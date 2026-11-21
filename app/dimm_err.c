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

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void dimm_err_init(void)
{
    for (int slot = 0; slot < MAX_SPD_SLOT; slot++) {
        slot_errors[slot] = 0;
    }
}

void dimm_err_insert(uint64_t pa)
{
    dram_loc_t loc;

    if (memctrl_decode_addr(pa, &loc) && loc.slot < MAX_SPD_SLOT) {
        slot_errors[loc.slot]++;
    }
}

uint64_t dimm_err_slot_errors(int slot)
{
    return (slot >= 0 && slot < MAX_SPD_SLOT) ? slot_errors[slot] : 0;
}
