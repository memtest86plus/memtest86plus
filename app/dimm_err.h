// SPDX-License-Identifier: GPL-2.0
#ifndef DIMM_ERR_H
#define DIMM_ERR_H
/**
 * \file
 *
 * Provides functions for recording errors per DIMM slot, using the memory
 * controller address decoder.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

#include <stdint.h>

/**
 * Initialises the per-DIMM error record.
 */
void dimm_err_init(void);

/**
 * Attributes an error at physical address pa to its DIMM slot, if the
 * address can be decoded.
 */
void dimm_err_insert(uint64_t pa);

/**
 * Returns the number of errors attributed to an SPD slot (spd_slot_cache[]
 * index).
 */
uint64_t dimm_err_slot_errors(int slot);

#endif // DIMM_ERR_H
