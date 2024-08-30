// SPDX-License-Identifier: GPL-2.0
#ifndef STRING_H
#define STRING_H
/**
 * \file
 *
 * Provides a subset of the functions normally provided by <string.h>.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stddef.h>

/**
 * Compares the first n bytes of the memory areas pointed to by s1 and s2
 * and returns 0 if all bytes are the same or the numerical difference
 * between the first mismatching byte in s1 (interpreted as an unsigned
 * value) and the corresponding byte in s2.
 */
static inline int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *src1 = s1, *src2 = s2;

    for (size_t i = 0; i < n; i++) {
        if (src1[i] != src2[i]) {
            return (int)src1[i] - (int)src2[i];
        }
    }
    return 0;
}

/**
 * Copies n bytes from the memory area pointed to by src to the memory area
 * pointed to by dest and returns a pointer to dest. The memory areas must
 * not overlap.
 * void *memcpy(void *dst, const void *src, size_t n);
 */
#if !(defined(DEBUG_GDB) || defined(__loongarch_lp64))
    #define memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#else
    void *memcpy (void *dest, const void *src, size_t len);
#endif

/**
 * Copies n bytes from the memory area pointed to by src to the memory area
 * pointed to by dest and returns a pointer to dest. The memory areas may
 * overlap.
 */
void *memmove(void *dest, const void *src, size_t n);

/**
 * Fills the first n bytes of the memory area pointed to by s with the byte
 * value c.
 * void *memset(void *s, int c, size_t n);
 */
#if !(defined(DEBUG_GDB) || defined(__loongarch_lp64))
    #define memset(s, c, n) __builtin_memset((s), (c), (n))
#else
    void *memset (void *dest, int val, size_t len);
#endif

/**
 * Returns the string length, excluding the terminating null character.
 */
static inline size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

/**
 * Compares at most the first n characters in the strings s1 and s2 and
 * returns 0 if all characters are the same or the numerical difference
 * between the first mismatching character in s1 (interpreted as a signed
 * value) and the corresponding character in s2.
 */
static inline int strncmp(const char *s1, const char *s2, size_t n)
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

/**
 * Finds the first occurrence of the substring needle in the string haystack
 * and returns a pointer to the beginning of the located substring, or NULL
 * if the substring is not found.
 */
char *strstr(const char *haystack, const char *needle);

/**
 * Convert n to characters in s
 */

char *itoa(int num, char *str);

/**
 * Convert a hex string to the corresponding 32-bit uint value.
 * returns 0 if a non-hex char is found (not 0-9/a-f/A-F).
 */

uint32_t hexstr2int(const char *hexstr);

#endif // STRING_H
