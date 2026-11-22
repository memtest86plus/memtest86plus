// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester.

#include <stdbool.h>
#include <stdint.h>

#include "memctrl.h"
#include "spd.h"

#include "dimm_err.h"

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static uint64_t slot_errors[MAX_SPD_SLOT];

// Errors known to be in one of two modules: counted on both, and once in the total.
static uint64_t slot_suspect[MAX_SPD_SLOT];
static uint64_t ambiguous;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void dimm_err_init(void)
{
    for (int slot = 0; slot < MAX_SPD_SLOT; slot++) {
        slot_errors[slot]  = 0;
        slot_suspect[slot] = 0;
    }
    ambiguous = 0;
}

void dimm_err_insert(uint64_t pa)
{
    dram_loc_t loc;

    if (!memctrl_decode_addr(pa, &loc)) {
        return;
    }
    if (loc.dimm != DRAM_DIMM_ANY) {
        if (loc.slot < MAX_SPD_SLOT) {
            slot_errors[loc.slot]++;
        }
        return;
    }
    ambiguous++;
    for (int slot = 0; slot < MAX_SPD_SLOT; slot++) {
        if (loc.slot_mask & (1 << slot)) {
            slot_suspect[slot]++;
        }
    }
}

uint64_t dimm_err_slot_errors(int slot)
{
    return (slot >= 0 && slot < MAX_SPD_SLOT) ? slot_errors[slot] : 0;
}

uint64_t dimm_err_slot_suspect(int slot)
{
    return (slot >= 0 && slot < MAX_SPD_SLOT) ? slot_suspect[slot] : 0;
}

uint64_t dimm_err_ambiguous(void)
{
    return ambiguous;
}
