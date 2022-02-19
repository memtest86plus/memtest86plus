// SPDX-License-Identifier: GPL-2.0
#ifndef TEMPERATURE_H
#define TEMPERATURE_H
/**
 * \file
 *
 * Provides a function to read the CPU core temperature.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

/**
 * Returns the current temperature of the CPU. Returns 0 if
 * the temperature cannot be read.
 */
int get_cpu_temperature(void);

#endif // TEMPERATURE_H
