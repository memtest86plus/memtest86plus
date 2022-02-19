// SPDX-License-Identifier: GPL-2.0
#ifndef HWCTRL_H
#define HWCTRL_H
/**
 * \file
 *
 * Provides miscellaneous hardware control functions.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

/**
 * Reboots the machine.
 */
void reboot(void);

/**
 * Turns off the floppy motor.
 */
void floppy_off();

/**
 * Disables the screen cursor.
 */
void cursor_off();

#endif // HWCTRL_H
