// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for IMC configuration, ECC support, etc.
//

#include <stdbool.h>

#include "config.h"
#include "cpuinfo.h"

#include "memctrl.h"
#include "mch/mch.h"

imc_info_t imc = {"UNDEF", 0, 0, 0, 0, 0, 0, 0};

ecc_info_t ecc_status = {false, ECC_ERR_NONE, 0, 0, 0, 0, 0};

// ---------------------
// -- Public function --
// ---------------------

void memctrl_init(void)
{
    ecc_status.ecc_enabled = false;

    if (!enable_mch_read) {
        return;
    }

    switch(imc_type) {
      case IMC_K17:
        get_imc_config_amd_k17();
        break;
      case IMC_SNB:
      case IMC_IVB:
        get_imc_config_intel_snb();
        break;
      case IMC_HSW:
         get_imc_config_intel_hsw();
         break;
      case IMC_SKL:
      case IMC_KBL:
         get_imc_config_intel_skl();
         break;
      default:
         break;
    }

    // Consistency check
    if (imc.tCL == 0 || imc.tRCD == 0 || imc.tRP == 0 || imc.tRCD == 0) {
        imc.freq = 0;
    }
}
