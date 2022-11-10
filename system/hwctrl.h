// SPDX-License-Identifier: GPL-2.0
#ifndef HWCTRL_H
#define HWCTRL_H
/**
 * \file
 *
 * Provides miscellaneous hardware control functions.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

/**
 * Initialises the hardware control interface.
 */
void hwctrl_init(void);

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

/**
 * Set date in the hardware realtime clock (RTC)
 *
 * year: 0-99, month: 1-12, day: 1-31
 */
void set_rtc_date(int year, int month, int day);

#endif // HWCTRL_H
