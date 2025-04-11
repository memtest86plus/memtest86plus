// SPDX-License-Identifier: GPL-2.0
#ifndef LOG_H
#define LOG_H
/**
 * \file
 *
 * Provides functions to print strings and formatted values to the serial tty.
 *
 *//*
 * Copyright (C) 2025 classabbyamp.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#define tty_log(...) \
    if (enable_tty_log) log_printf(__VA_ARGS__)

/**
 * Prints a single character on serial tty.
 */
void log_printc(char c);

/**
 * Prints a string on serial tty.
 */
void log_prints(const char *str);

/**
 * Prints a signed decimal number on serial tty.
 */
void log_printi(int value);

/**
 * Prints an unsigned decimal number on serial tty.
 */
void log_printu(uintptr_t value);

/**
 * Prints an unsigned hexadecimal number on serial tty.
 */
void log_printx(uintptr_t value);

/**
 * Prints a K<unit> value on serial tty. The value is shown to 3 significant
 * figures in the nearest K/M/G/T units.
 */
void log_printk(uintptr_t value);

/**
 * Emulates the standard printf function.
 *
 * The conversion specifiers supported are:
 *   c  character (int type)
 *   s  string (char* type)
 *   i  signed decimal integer (int type)
 *   u  unsigned decimal integer (uintptr_t type)
 *   x  unsigned hexadecimal integer (uintptr_t type)
 *   k  K<unit> value (scaled to K/M/G/T) (uintptr_t type)
 */
void log_printf(const char *fmt, ...);

/**
 * The alternate form of printf.
 */
void log_vprintf(const char *fmt, va_list args);

#endif // LOG_H

