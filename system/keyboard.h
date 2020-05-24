// SPDX-License-Identifier: GPL-2.0
#ifndef KEYBOARD_H
#define KEYBOARD_H
/*
 * Provides the keyboard interface. It converts incoming key codes to
 * ASCII characters.
 *
 * Copyright (C) 2020 Martin Whitaker.
 */

/*
 * The Escape character.
 */
#define ESC     27

/*
 * Checks if a key has been pressed and returns the primary ASCII character
 * corresponding to that key if so, otherwise returns the null character.
 * F1 to F10 are mapped to the corresponding decimal digit (F10 -> 0). All
 * other keys that don't have a corresponding ASCII character are ignored.
 * Characters are only returned for key presses, not key releases. A US
 * keyboard layout is assumed (because we can't easily do anything else).
 */
char get_key(void);

#endif // KEYBOARD_H
