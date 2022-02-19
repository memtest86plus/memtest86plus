// SPDX-License-Identifier: GPL-2.0
#ifndef CTYPE_H
#define CTYPE_H
/**
 * \file
 *
 * Provides a subset of the functions normally provided by <ctype.h>.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

/**
 * If c is a lower-case letter, returns its upper-case equivalent, otherwise
 * returns c. Assumes c is an ASCII character.
 */
int toupper(int c);

/**
 * Returns 1 if c is a decimal digit, otherwise returns 0. Assumes c is an
 * ASCII character.
 */
int isdigit(int c);

/**
 * Returns 1 if c is a hexadecimal digit, otherwise returns 0. Assumes c is an
 * ASCII character.
 */
int isxdigit(int c);

#endif // CTYPE_H
