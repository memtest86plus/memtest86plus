// SPDX-License-Identifier: GPL-2.0
#ifndef TEMPERATURE_H
#define TEMPERATURE_H
/**
 * \file
 *
 * Provides a function to read the CPU core temperature.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 * Copyright (C) 2003-2023 Sam Demeulemeester.
 */

#define AMD_TEMP_REG_K8     0xE4
#define AMD_TEMP_REG_K10    0xA4

// Temp Registers on AMD ZEN System Management Network
#define SMN_SMUIO_THM               0x00059800
#define SMN_THM_TCON_CUR_TMP        (SMN_SMUIO_THM + 0x00)

/**
 * Global CPU Temperature offset
 */
extern float cpu_temp_offset;

/**
 * Init temperature sensor and compute offsets if needed
 */
void temperature_init(void);

/**
 * Returns the current temperature of the CPU. Returns 0 if
 * the temperature cannot be read.
 */
int get_cpu_temperature(void);

#endif // TEMPERATURE_H
