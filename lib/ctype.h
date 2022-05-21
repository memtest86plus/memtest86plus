// SPDX-License-Identifier: GPL-2.0
#ifndef CTYPE_H
#define CTYPE_H
/**
 * \file
 *
 * Provides a subset of the functions normally provided by <ctype.h>.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

/**
 * If c is a lower-case letter, returns its upper-case equivalent, otherwise
 * returns c. Assumes c is an ASCII character.
 */
static inline int toupper(int c)
{
    if (c >= 'a' && c <= 'z') {
        return c + 'A' -'a';
    } else {
        return c;
    }
}

/**
 * Returns 1 if c is a decimal digit, otherwise returns 0. Assumes c is an
 * ASCII character.
 */
static inline int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

/**
 * Returns 1 if c is a hexadecimal digit, otherwise returns 0. Assumes c is an
 * ASCII character.
 */
static inline int isxdigit(int c)
{
    return isdigit(c) || (toupper(c) >= 'A' && toupper(c) <= 'F');
}

#endif // CTYPE_H
