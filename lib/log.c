// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 classabbyamp.

#include <stdbool.h>
#include <stdint.h>

#include "serial.h"

#include "string.h"

#include "log.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define BUFFER_SIZE 64

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static int int_to_dec_str(char buffer[], int value, int max_length)
{
    char temp[max_length];
    bool negative = (value < 0);
    if (negative) {
        value = -value;
        max_length--;
    }

    int length = 0;

    if (value == 0) {
        temp[length++] = '0';
    }

    while (value > 0 && length < max_length) {
        temp[length++] = '0' + (value % 10);
        value /= 10;
    }
    if (negative) {
        temp[length++] = '-';
    }
    for (int i = length; i > 0; i--) {
        buffer[length - i] = temp[i - 1];
    }
    return length;
}

static int uint_to_dec_str(char buffer[], uintptr_t value, int max_length)
{
    char temp[max_length];
    int length = 0;

    if (value == 0) {
        temp[length++] = '0';
    }

    while (value > 0 && length < max_length) {
        temp[length++] = '0' + (value % 10);
        value /= 10;
    }
    for (int i = length; i > 0; i--) {
        buffer[length - i] = temp[i - 1];
    }
    return length;
}

static int uint_to_hex_str(char buffer[], uintptr_t value, int max_length)
{
    char temp[max_length];
    int length = 0;

    if (value == 0) {
        temp[length++] = '0';
    }

    while (value > 0 && length < max_length) {
        int digit = value % 16;
        if (digit < 10) {
            temp[length++] = '0' + digit;
        } else {
            temp[length++] = 'a' + digit - 10;
        }
        value /= 16;
    }
    for (int i = length; i > 0; i--) {
        buffer[length - i] = temp[i - 1];
    }
    return length;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void log_printc(const char c)
{
    tty_echo_print(&c);
}

void log_prints(const char *str)
{
    tty_echo_print(str);
}

void log_printi(int value)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    int_to_dec_str(buffer, value, BUFFER_SIZE);
    log_prints(buffer);
}

void log_printu(uintptr_t value)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    uint_to_dec_str(buffer, value, BUFFER_SIZE);
    log_prints(buffer);
}

void log_printx(uintptr_t value)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    uint_to_hex_str(buffer, value, BUFFER_SIZE);
    log_prints(buffer);
}

void log_printk(uintptr_t value)
{
    static const char suffix[4] = { 'K', 'M', 'G', 'T' };

    int scale = 0;
    int fract = 0;
    while (value >= 1024 && scale < (int)(sizeof(suffix) - 1)) {
        fract = value % 1024;
        value /= 1024;
        scale++;
    }
    int fract_length = 0;
    if (fract > 0) {
        if (value < 10) {
            fract = (100 * fract) / 1024;
            if (fract > 0) {
                if (fract % 10) {
                    fract_length = 2;
                } else {
                    fract_length = 1;
                    fract /= 10;
                }
            }
        } else if (value < 100) {
            fract = (100 * fract) / (10 * 1024);
            if (fract > 0) {
                fract_length = 1;
            }
        }
    }

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    int length = 0;

    length += uint_to_dec_str(&buffer[length], value, BUFFER_SIZE);

    if (fract_length > 0) {
        buffer[length++] = '.';
        length += int_to_dec_str(&buffer[length], fract, BUFFER_SIZE - length);
    }

    buffer[length++] = suffix[scale];

    log_prints(buffer);
}

void log_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_vprintf(fmt, args);
    va_end(args);
}

void log_vprintf(const char *fmt, va_list args)
{
    while (*fmt) {
        if (*fmt != '%') {
            log_printc(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            log_printc(*fmt++);
            continue;
        }

        int length = 0;
        if (*fmt == '*') {
            length = va_arg(args, int);
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                length = 10 * length + *fmt - '0';
                fmt++;
            }
        }
        switch (*fmt) {
          case 'c': {
            char buffer[1];
            buffer[0] = va_arg(args, int);
            log_prints(buffer);
          } break;
          case 's': {
            const char *str = va_arg(args, char *);
            log_prints(str);
          } break;
          case 'i':
            log_printi(va_arg(args, int));
            break;
          case 'u':
            log_printu(va_arg(args, uintptr_t));
            break;
          case 'x':
            log_printx(va_arg(args, uintptr_t));
            break;
          case 'k':
            log_printk(va_arg(args, uintptr_t));
            break;
        }
        fmt++;
    }
}
