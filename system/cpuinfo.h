// SPDX-License-Identifier: GPL-2.0
#ifndef CPUINFO_H
#define CPUINFO_H
/**
 * \file
 *
 * Provides information about the CPU type, clock speed and cache sizes.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * A string identifying the CPU make and model.
 */
extern const char *cpu_model;

/**
 * A number identifying the integrated memory controller type.
 */
extern uint32_t imc_type;

/**
 * The size of the L1 cache in KB.
 */
extern int l1_cache;

/**
 * The size of the L2 cache in KB.
 */
extern int l2_cache;

/**
 * The size of the L3 cache in KB.
 */
extern int l3_cache;

/**
 * A flag indicating that we can't read the core temperature on this CPU.
 */
extern bool no_temperature;

/**
 * The TSC clock speed in kHz. Assumed to be the nominal CPU clock speed.
 */
extern uint32_t clks_per_msec;

/**
 * Determines the CPU info and stores it in the exported variables.
 */
void cpuinfo_init(void);

#endif // CPUINFO_H
