// SPDX-License-Identifier: GPL-2.0
#ifndef PRINT_H
#define PRINT_H
/**
 * \file
 *
 * Provides functions to print strings and formatted values to the screen.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Prints a single character on screen at location (row,col) and returns col+1.
 */
int printc(int row, int col, char c);

/**
 * Prints a string on screen starting at location (row,col) and returns the
 * next column after the string.
 */
int prints(int row, int col, const char *str);

/**
 * Prints a signed decimal number on screen starting at location (row,col) in
 * a field of at least length characters, optionally padding the number with
 * leading zeros, and optionally left-justifying instead of right-justifying
 * in the field. Returns the next column after the formatted number.
 */
int printi(int row, int col, int value, int length, bool pad, bool left);

/**
 * Prints an unsigned decimal number on screen starting at location (row,col)
 * in a field of at least length characters, optionally padding the number with
 * leading zeros, and optionally left-justifying instead of right-justifying in
 * the field. Returns the next column after the formatted number.
 */
int printu(int row, int col, uintptr_t value, int length, bool pad, bool left);

/**
 * Prints an unsigned hexadecimal number on screen starting at location (row,col)
 * in a field of at least length characters, optionally padding the number with
 * leading zeros, and optionally left-justifying instead of right-justifying in
 * the field. Returns the next column after the formatted number.
 */
int printx(int row, int col, uintptr_t value, int length, bool pad, bool left);

/**
 * Prints a K<unit> value on screen starting at location (row,col) in a field of
 * at least length characters, optionally padding the number with leading zeros,
 * and optionally left-justifying instead of right-justifying in the field. The
 * value is shown to 3 significant figures in the nearest K/M/G/T units. Returns
 * the next column after the formatted value.
 */
int printk(int row, int col, uintptr_t value, int length, bool pad, bool left);

/**
 * Emulates the standard printf function. Printing starts at location (row,col).
 *
 * The conversion flags supported are:
 *   -  left justify
 *   0  pad with leading zeros
 *
 * The conversion specifiers supported are:
 *   c  character (int type)
 *   s  string (char* type)
 *   i  signed decimal integer (int type)
 *   u  unsigned decimal integer (uintptr_t type)
 *   x  unsigned hexadecimal integer (uintptr_t type)
 *   k  K<unit> value (scaled to K/M/G/T) (uintptr_t type)
 *
 * The only other conversion option supported is the minimum field width. This
 * may be either a literal value or '*'.
 *
 * Returns the next column after the formatted string.
 */
int printf(int row, int col, const char *fmt, ...);

/**
 * The alternate form of printf.
 */
int vprintf(int row, int col, const char *fmt, va_list args);

#endif // PRINT_H
