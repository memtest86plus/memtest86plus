// SPDX-License-Identifier: GPL-2.0
#ifndef READ_H
#define READ_H
/**
 * \file
 *
 * Provides a function to read a numeric value.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdint.h>

/**
 * Returns an unsigned numeric value entered on the keyboard. Echoes the
 * input to the display field located at (row,col), limiting it to field_width
 * characters. If the entered value is prefixed by "0x", assumes base 16,
 * otherwise assumes base 10. If the value is suffixed by 'K', 'P', 'M',
 * 'G', or 'T', the returned value will be scaled by 2^10, 2^12, 2^20,
 * 2^30, or 2^40 accordingly. The returned value will also be scaled by
 * 2^shift.
 */
uintptr_t read_value(int x, int y, int field_width, int shift);

#endif // READ_H
