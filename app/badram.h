// SPDX-License-Identifier: GPL-2.0
#ifndef BADRAM_H
#define BADRAM_H
/**
 * \file
 *
 * Provides functions for recording and displaying faulty address locations
 * in a condensed form. The display format is determined by the current value
 * of the error_mode config setting as follows:
 *
 *  - ERROR_MODE_BADRAM
 *      records and displays patterns in the format used by the Linux BadRAM
 *      extension or GRUB badram command
 *
 *  - ERROR_MODE_MEMMAP
 *      records and displays address ranges in the format used by the Linux
 *      memmap boot command line option
 *
 *  - ERROR_MODE_PAGES
 *      records and displays memory page numbers
 *
 *//*
 * Copyright (C) 2020-2024 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include "test.h"

/**
 * Initialises the fault record. This must be called each time error_mode is
 * changed.
 */
void badram_init(void);

/**
 * Inserts a single faulty address into the fault record. Returns true iff
 * the fault record was changed.
 */
bool badram_insert(testword_t page, testword_t offset);

/**
 * Displays the fault record in the scrollable display region in the format
 * determined by error_mode.
 */
void badram_display(void);

#endif // BADRAM_H
