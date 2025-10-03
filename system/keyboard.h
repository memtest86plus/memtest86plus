// SPDX-License-Identifier: GPL-2.0
#ifndef KEYBOARD_H
#define KEYBOARD_H
/**
 * \file
 *
 * Provides the keyboard interface. It converts incoming key codes to
 * ASCII characters.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>

/**
 * The Escape character.
 */
#define ESC     27

/**
 * A set of supported keyboard types.
 */
typedef enum {
    KT_NONE    = 0,
    KT_LEGACY  = 1,
    KT_USB     = 2
} keyboard_types_t;

/**
 * The set of enabled keyboard types
 */
extern keyboard_types_t keyboard_types;

/**
 * Initialises the keyboard interface.
 */
void keyboard_init(void);

/**
 * Checks if a key has been pressed and returns the primary ASCII character
 * corresponding to that key if so, otherwise returns the null character.
 * F1 to F10 are mapped to the corresponding decimal digit (F10 -> 0). All
 * other keys that don't have a corresponding ASCII character are ignored.
 * Characters are only returned for key presses, not key releases. A US
 * keyboard layout is assumed (because we can't easily do anything else).
 */
char get_key(void);

/**
 * The key to use to 'escape', e.g. reboot / exit.
 * Use this function rather than hardcoding 'ESC' to work well with TTY mode.
 */
char get_logical_escape_key(void);

/**
 * String label for the logical escape key.
 */
const char *get_logical_escape_key_label(void);

/**
 * String label for function keys.
 */
const char *get_function_key_label(int num);

#endif // KEYBOARD_H
