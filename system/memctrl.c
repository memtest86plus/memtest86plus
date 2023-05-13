// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for IMC configuration, ECC support, etc.
//

#include <stdbool.h>

#include "error.h"

#include "config.h"

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"
#include "vmem.h"

#include "imc/imc.h"
#include "imc/amd_zen.h"
#include "imc/intel_snb.h"
#include "imc/intel_hsw.h"
#include "imc/intel_skl.h"
#include "imc/intel_icl.h"
#include "imc/intel_adl.h"

#include "display.h"

imc_info_t imc = {"UNDEF", 0, 0, 0, 0, 0, 0, 0, 0};

ecc_info_t ecc_status = {false, ECC_ERR_NONE, 0, 0, 0, 0};

// ---------------------
// -- Public function --
// ---------------------

void memctrl_init(void)
{
    ecc_status.ecc_enabled = false;

    if (!enable_mch_read) {
        return;
    }

    switch(imc.family) {
      case IMC_K17:
      case IMC_K19_VRM:
      case IMC_K19_RPL:
      case IMC_K19_RBT:
        get_imc_config_amd_zen();
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
      case IMC_RKL:
        get_imc_config_intel_icl();
        break;
      case IMC_RPL:
      case IMC_ADL:
        get_imc_config_intel_adl();
        break;
      default:
        break;
    }

    // Consistency check
    if (imc.tCL == 0 || imc.tRCD == 0 || imc.tRP == 0 || imc.tRCD == 0) {
        imc.freq = 0;
    }
}

void memctrl_poll_ecc(void)
{
    if (!ecc_status.ecc_enabled) {
        return;
    }

    switch(imc.family) {
      case IMC_K17:
      case IMC_K19_VRM:
      case IMC_K19_RPL:
      case IMC_K19_RBT:
        poll_ecc_amd_zen(true);
        break;
      default:
        break;
    }
}
