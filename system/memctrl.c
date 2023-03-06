// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for IMC configuration, ECC support, etc.
//

#include <stdbool.h>

#include "cpuinfo.h"

#include "memctrl.h"
#include "mch/mch.h"

imc_info_t imc_status;

ecc_info_t ecc_status;

// ---------------------
// -- Public function --
// ---------------------

void memctrl_init(void)
{
    imc_status.freq     = 0;
    imc_status.tCL      = 0;
    imc_status.tCL_dec  = 0;
    imc_status.tRCD     = 0;
    imc_status.tRP      = 0;
    imc_status.tRAS     = 0;

    ecc_status.ecc_enabled = false;

    switch(imc_type)
    {
        case IMC_HSW:
            get_imc_config_intel_hsw();
            break;
        case IMC_SKL:
            get_imc_config_intel_skl();
            break;
        default:
            break;
    }
}
