// SPDX-License-Identifier: GPL-2.0
#ifndef BAD_PAGES_LIST_H
#define BAD_PAGES_LIST_H
/**
 * \file
 *
 * Provides functions for displaying a list of bad pages for use with window's
 * badmemorylist
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include "test.h"

/**
 * TODO
 */
void bad_pages_list_init(void);

/**
 * TODO
 */
bool bad_pages_list_insert(testword_t page);

/**
 * TODO
 */
void bad_pages_list_display(void);

#endif // BAD_PAGES_LIST_H
