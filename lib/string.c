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

#include "string.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *src1 = s1, *src2 = s2;

    for (size_t i = 0; i < n; i++) {
        if (src1[i] != src2[i]) {
            return (int)src1[i] - (int)src2[i];
        }
    }
    return 0;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *d = (char *)dest, *s = (char *)src;

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

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

void *memset(void *s, int c, size_t n)
{
    char *d = (char *)s;

    for (size_t i = 0; i < n; i++) {
        d[i] = c;
    }
    return s;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (int)s1[i] - (int)s2[i];
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

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
