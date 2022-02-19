// SPDX-License-Identifier: GPL-2.0
#ifndef STRING_H
#define STRING_H
/**
 * \file
 *
 * Provides a subset of the functions normally provided by <string.h>.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stddef.h>

/**
 * Compares the first n bytes of the memory areas pointed to by s1 and s2
 * and returns 0 if all bytes are the same or the numerical difference
 * between the first mismatching byte in s1 (interpreted as an unsigned
 * value) and the corresponding byte in s2.
 */
int memcmp(const void *s1, const void *s2, size_t n);

/**
 * Copies n bytes from the memory area pointed to by src to the memory area
 * pointed to by dest and returns a pointer to dest. The memory areas must
 * not overlap.
 */
void *memcpy(void *dst, const void *src, size_t n);

/**
 * Copies n bytes from the memory area pointed to by src to the memory area
 * pointed to by dest and returns a pointer to dest. The memory areas may
 * overlap.
 */
void *memmove(void *dest, const void *src, size_t n);

/**
 * Fills the first n bytes of the memory area pointed to by s with the byte
 * value c.
 */
void *memset(void *s, int c, size_t n);

/**
 * Returns the string length, excluding the terminating null character.
 */
size_t strlen(const char *s);

/**
 * Compares at most the first n characters in the strings s1 and s2 and
 * returns 0 if all characters are the same or the numerical difference
 * between the first mismatching character in s1 (interpreted as a signed
 * value) and the corresponding character in s2.
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * Finds the first occurrence of the substring needle in the string haystack
 * and returns a pointer to the beginning of the located substring, or NULL
 * if the substring is not found.
 */
char *strstr(const char *haystack, const char *needle);

#endif // STRING_H
