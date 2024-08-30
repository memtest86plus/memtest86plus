// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.
//
// Derived from an extract of memtest86+ lib.c:
//
// lib.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stddef.h>
#include <stdint.h>

#include "string.h"

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

void reverse(char s[])
{
    int i, j;
    char c;

    for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}
//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void *memmove(void *dest, const void *src, size_t n)
{
    char *d = (char *)dest, *s = (char *)src;

    if (n > 0) {
        if (dest < src) {
            for (size_t i = 0; i < n; i++) {
                d[i] = s[i];
            }
        }
        if (dest > src) {
            size_t i = n;
            do {
                i--;
                d[i] = s[i];
            } while (i > 0);
        }
    }
    return dest;
}

#if (defined(DEBUG_GDB) || defined(__loongarch_lp64))

void *memcpy (void *dest, const void *src, size_t len)
{
    char *d = dest;
    const char *s = src;
    while (len--)
        *d++ = *s++;
    return dest;
}

void *memset (void *dest, int val, size_t len)
{
  unsigned char *ptr = dest;
  while (len-- > 0)
    *ptr++ = val;
  return dest;
}

#endif

char *strstr(const char *haystack, const char *needle)
{
    size_t haystack_len = strlen(haystack);
    size_t needle_len   = strlen(needle);

    size_t max_idx = haystack_len - needle_len;

    for (size_t idx = 0; idx <= max_idx; idx++) {
        if (memcmp(haystack + idx, needle, needle_len) == 0) {
            return (char *)haystack + idx;
        }
    }
    return NULL;
}

char *itoa(int num, char *str)
{
    int i = 0;

    /* Special case for 0 */
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    // Parse digits
    while (num != 0) {
        int rem = num % 10;
        str[i++] = (rem > 9) ? (rem-10) + 'a' : rem + '0';
        num /= 10;
    }

    // Last is string terminator
    str[i] = '\0';

    reverse(str);

    return str;
}

uint32_t hexstr2int(const char *hexstr) {
    uint32_t ival = 0;
    while (*hexstr) {
        uint8_t b = *hexstr++;

        if (b >= '0' && b <= '9') b = b - '0';
        else if (b >= 'a' && b <='f') b = b - 'a' + 10;
        else if (b >= 'A' && b <='F') b = b - 'A' + 10;
        else return 0;

        ival = (ival << 4) | (b & 0xF);
    }
    return ival;
}
