// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2021 Martin Whitaker.

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"

#include "string.h"

#include "print.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define BUFFER_SIZE 64

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static int int_to_dec_str(char buffer[], int value, int min_length, int max_length)
{
    bool negative = (value < 0);
    if (negative) {
        value = -value;
        if (min_length > 1) {
            min_length--;
        }
        max_length--;
    }

    int length = 0;
    while (length < min_length || (value > 0 && length < max_length)) {
        buffer[length++] = '0' + (value % 10);
        value /= 10;
    }
    if (negative) {
        buffer[length++] = '-';
    }
    return length;
}

static int uint_to_dec_str(char buffer[], uintptr_t value, int min_length, int max_length)
{
    int length = 0;
    while (length < min_length || (value > 0 && length < max_length)) {
        buffer[length++] = '0' + (value % 10);
        value /= 10;
    }
    return length;
}

static int uint_to_hex_str(char buffer[], uintptr_t value, int min_length, int max_length)
{
    int length = 0;
    while (length < min_length || (value > 0 && length < max_length) ){
        int digit = value % 16;
        if (digit < 10) {
            buffer[length++] = '0' + digit;
        } else {
            buffer[length++] = 'a' + digit - 10;
        }
        value /= 16;
    }
    return length;
}

static int min_str_length(int field_length, bool pad)
{
    return (field_length > 0 && pad) ? field_length : 1;
}

static int print_in_field(int row, int col, const char buffer[], int buffer_length, int field_length, bool left)
{
    bool reversed = false;
    if (buffer_length < 0) {
        buffer_length = -buffer_length;
        reversed = true;
    }
    if (!left) {
        while (field_length > buffer_length) {
            print_char(row, col++, ' ');
            field_length--;
        }
    }
    if (reversed) {
        for (int i = buffer_length - 1; i >= 0; i--) {
            print_char(row, col++, buffer[i]);
        }
    } else {
        for (int i = 0; i < buffer_length; i++) {
            print_char(row, col++, buffer[i]);
        }
    }
    if (left) {
        while (field_length > buffer_length) {
            print_char(row, col++, ' ');
            field_length--;
        }
    }
    return col;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int printc(int row, int col, const char c)
{
    print_char(row, col++, c);
    return col;
}

int prints(int row, int col, const char *str)
{
    while (*str) {
        print_char(row, col++, *str++);
    }
    return col;
}

int printi(int row, int col, int value, int field_length, bool pad, bool left)
{
    char buffer[BUFFER_SIZE];

    int length = int_to_dec_str(buffer, value, min_str_length(field_length, pad), BUFFER_SIZE);

    return print_in_field(row, col, buffer, -length, field_length, left);
}

int printu(int row, int col, uintptr_t value, int field_length, bool pad, bool left)
{
    char buffer[BUFFER_SIZE];

    int length = uint_to_dec_str(buffer, value, min_str_length(field_length, pad), BUFFER_SIZE);

    return print_in_field(row, col, buffer, -length, field_length, left);
}

int printx(int row, int col, uintptr_t value, int field_length, bool pad, bool left)
{
    char buffer[BUFFER_SIZE];

    int length = uint_to_hex_str(buffer, value, min_str_length(field_length, pad), BUFFER_SIZE);

    return print_in_field(row, col, buffer, -length, field_length, left);
}

int printk(int row, int col, uintptr_t value, int field_length, bool pad, bool left, bool add_space)
{
    static const char suffix[4] = { 'K', 'M', 'G', 'T' };

    int scale = 0;
    int fract = 0;
    while (value >= 1024 && scale < (int)(sizeof(suffix) - 1)) {
        fract = value % 1024;
        value /= 1024;
        scale++;
    }
    int whole_length = field_length > 1 ? field_length - 1 : 0;
    int fract_length = 0;
    if (fract > 0) {
        if (value < 10) {
            whole_length = field_length > 4 ? field_length - 4 : 0;
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
            whole_length = field_length > 3 ? field_length - 3 : 0;
            fract = (100 * fract) / (10 * 1024);
            if (fract > 0) {
                fract_length = 1;
            }
        }
    }

    char buffer[BUFFER_SIZE];

    int length = 0;
    buffer[length++] = suffix[scale];

    if(add_space) {
        buffer[length++] = ' ';
    }

    if (fract_length > 0) {
        length += int_to_dec_str(&buffer[length], fract, fract_length, fract_length);
        buffer[length++] = '.';
    }
    length += uint_to_dec_str(&buffer[length], value, min_str_length(whole_length, pad), BUFFER_SIZE - length);

    return print_in_field(row, col, buffer, -length, field_length, left);
}

int printf(int row, int col, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    int end_col = vprintf(row, col, fmt, args);
    va_end(args);

    return end_col;
}

int vprintf(int row, int col, const char *fmt, va_list args)
{
    while (*fmt) {
        if (*fmt != '%') {
            print_char(row, col++, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            print_char(row, col++, *fmt++);
            continue;
        }

        bool pad        = false;
        bool left       = false;
        bool add_space  = false;
        int length = 0;
        if (*fmt == '-') {
            left = true;
            fmt++;
        }
        if (*fmt == 'S') {
            add_space = true;
            fmt++;
        }
        if (*fmt == '0') {
            pad = !left;
            fmt++;
        }
        if (*fmt == '*') {
            length = va_arg(args, int);
            if (length < 0) {
                length = -length;
                left = true;
            }
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
            col = print_in_field(row, col, buffer, 1, length, left);
          } break;
          case 's': {
            const char *str = va_arg(args, char *);
            col = print_in_field(row, col, str, strlen(str), length, left);
          } break;
          case 'i':
            col = printi(row, col, va_arg(args, int), length, pad, left);
            break;
          case 'u':
            col = printu(row, col, va_arg(args, uintptr_t), length, pad, left);
            break;
          case 'x':
            col = printx(row, col, va_arg(args, uintptr_t), length, pad, left);
            break;
          case 'k':
            col = printk(row, col, va_arg(args, uintptr_t), length, pad, left, add_space);
            break;
        }
        fmt++;
    }

    return col;
}
