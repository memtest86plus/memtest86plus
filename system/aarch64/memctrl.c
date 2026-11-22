// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for IMC configuration, ECC support, etc.
//

#include <stdbool.h>

#include "config.h"
#include "cpuinfo.h"
#include "smbios.h"
#include "spd.h"

#include "memctrl.h"
#include "imc/imc.h"

#include "display.h"

imc_info_t imc = {"UNDEF", 0, 0, 0, 0, 0, 0, 0, 0};

ecc_info_t ecc_status = {false, ECC_ERR_NONE, 0, 0, 0, 0};

// ---------------------
// -- Public function --
// ---------------------

void memctrl_init(void)
{
    // Memory controller detection and ECC are not supported yet on ARM64.
    ecc_status.ecc_enabled = false;
}

void memctrl_poll_ecc(void)
{
    // Nothing to do.
}

void memctrl_decode_init(void)
{
    static const uint8_t slot_order[MAX_SPD_SLOT] = { 0, 1, 2, 3, 4, 5, 6, 7 };

    smbios_map_spd_slots(slot_order);
}

bool memctrl_decode_addr(uint64_t pa, dram_loc_t *loc)
{
    (void)pa; (void)loc;
    return false;
}

const char *memctrl_decode_status(void)
{
    return "unsupported platform";
}
