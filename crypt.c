/* crypt.h -- base code for traditional PKWARE encryption
   Version 1.01e, February 12th, 2005

   Copyright (C) 1998-2005 Gilles Vollant
   Modifications for Info-ZIP crypting
     Copyright (C) 2003 Terry Thorsen

   This code is a modified version of crypting code in Info-ZIP distribution

   Copyright (C) 1990-2000 Info-ZIP.  All rights reserved.

   This program is distributed under the terms of the same license as zlib.
   See the accompanying LICENSE file for the full text of the license.

   The encryption/decryption parts of this source code (as opposed to the
   non-echoing password parts) were originally written in Europe. The
   whole source package can be freely distributed, including from the USA.
   (Prior to January 2000, re-export from the US was a violation of US law.)

   This encryption code is a direct transcription of the algorithm from
   Roger Schlafly, described by Phil Katz in the file appnote.txt. This
   file (appnote.txt) is distributed with the PKZIP program (even in the
   version without encryption capabilities).

   If you don't need crypting in your application, just define symbols
   NOCRYPT and NOUNCRYPT.
   
   Mar 8th, 2016 - Lucio Cosmo 
   Fixed support for 64bit builds for archives with "PKWARE" password.
   Changed long, unsigned long, unsigned to unsigned int in 
   access functions to crctables and pkeys
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

#include "crypt.h"

/***************************************************************************/

#define CRC32(c, b) ((*(pcrc_32_tab+(((uint32_t)(c) ^ (b)) & 0xff))) ^ ((c) >> 8))

/***************************************************************************/

uint8_t decrypt_byte(uint32_t *pkeys)
{
    unsigned temp;  /* POTENTIAL BUG:  temp*(temp^1) may overflow in an
                     * unpredictable manner on 16-bit systems; not a problem
                     * with any known compiler so far, though */

    temp = ((uint32_t)(*(pkeys+2)) & 0xffff) | 2;
    return (uint8_t)(((temp * (temp ^ 1)) >> 8) & 0xff);
}

uint8_t update_keys(uint32_t *pkeys, const uint32_t *pcrc_32_tab, uint8_t c)
{
    (*(pkeys+0)) = CRC32((*(pkeys+0)), c);
    (*(pkeys+1)) += (*(pkeys+0)) & 0xff;
    (*(pkeys+1)) = (*(pkeys+1)) * 134775813L + 1;
    {
        register uint32_t keyshift = (uint32_t)((*(pkeys + 1)) >> 24);
        (*(pkeys+2)) = CRC32((*(pkeys+2)), keyshift);
    }
    return c;
}

void init_keys(const char *passwd, uint32_t *pkeys, const uint32_t *pcrc_32_tab)
{
    *(pkeys+0) = 305419896L;
    *(pkeys+1) = 591751049L;
    *(pkeys+2) = 878082192L;
    while (*passwd != 0)
    {
        update_keys(pkeys, pcrc_32_tab, (uint8_t)*passwd);
        passwd += 1;
    }
}

/***************************************************************************/

int cryptrand(unsigned char *buf, unsigned int len)
{
    int rlen = 0;
#ifdef _WIN32
    HCRYPTPROV provider;
    unsigned __int64 pentium_tsc[1];
    int result = 0;


    if (CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
    {
        result = CryptGenRandom(provider, len, buf);
        CryptReleaseContext(provider, 0);
        if (result)
            return len;
    }

    for (rlen = 0; rlen < (int)len; ++rlen)
    {
        if (rlen % 8 == 0)
            QueryPerformanceCounter((LARGE_INTEGER *)pentium_tsc);
        buf[rlen] = ((unsigned char*)pentium_tsc)[rlen % 8];
    }
#else
    int frand = open("/dev/urandom", O_RDONLY);
    if (frand != -1)
    {
        rlen = (int)read(frand, buf, len);
        close(frand);
    }
#endif

    return rlen;
}

int crypthead(const char *passwd, uint8_t *buf, int buf_size,
              uint32_t *pkeys, const uint32_t *pcrc_32_tab, uint32_t crc_for_crypting)
{
    uint8_t n = 0;                      /* index in random header */
    uint8_t c = 0;                      /* random byte */
    uint8_t header[RAND_HEAD_LEN-2];    /* random header */
    uint16_t t = 0;                     /* temporary */

    if (buf_size < RAND_HEAD_LEN)
        return 0;

    init_keys(passwd, pkeys, pcrc_32_tab);

    /* First generate RAND_HEAD_LEN-2 random bytes. */
    cryptrand(header, RAND_HEAD_LEN-2);

    /* Encrypt random header (last two bytes is high word of crc) */
    init_keys(passwd, pkeys, pcrc_32_tab);

    for (n = 0; n < RAND_HEAD_LEN-2; n++)
        buf[n] = (uint8_t)zencode(pkeys, pcrc_32_tab, header[n], t);

    buf[n++] = (uint8_t)zencode(pkeys, pcrc_32_tab, (uint8_t)((crc_for_crypting >> 16) & 0xff), t);
    buf[n++] = (uint8_t)zencode(pkeys, pcrc_32_tab, (uint8_t)((crc_for_crypting >> 24) & 0xff), t);
    return n;
}

/***************************************************************************/