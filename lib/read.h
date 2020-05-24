// SPDX-License-Identifier: GPL-2.0
#ifndef READ_H
#define READ_H
/*
 * Provides a function to read a numeric value.
 *
 * Copyright (C) 2020 Martin Whitaker.
 */

#include <stdint.h>

/*
 * Returns an unsigned numeric value entered on the keyboard. Echoes the
 * input to the display field located at (row,col), limiting it to field_width
 * characters. If the entered value is prefixed by "0x", assumes base 16,
 * otherwise assumes base 10. If the value is suffixed by 'K', 'P', 'M', or
 * 'G', the returned value will be scaled by 10^10, 10^12, 10^20, or 10^30
 * accordingly.
 */
uintptr_t read_value(int x, int y, int field_width);

#endif // READ_H
