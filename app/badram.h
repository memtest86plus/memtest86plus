// SPDX-License-Identifier: GPL-2.0
#ifndef BADRAM_H
#define BADRAM_H
/**
 * \file
 *
 * Provides functions for generating patterns for the Linux kernel BadRAM extension.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialises the pattern array.
 */
void badram_init(void);

/**
 * Inserts a single faulty address into the pattern array. Returns
 * true iff the array was changed.
 */
bool badram_insert(uintptr_t addr);

/**
 * Displays the pattern array in the scrollable display region in the
 * format used by the Linux kernel.
 */
void badram_display(void);

#endif // BADRAM_H
