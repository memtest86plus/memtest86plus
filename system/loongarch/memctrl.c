// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
//
// ------------------------
//
// Platform-specific code for IMC configuration, ECC support, etc.
//

#include <stdbool.h>

#include "config.h"
#include "cpuinfo.h"

#include "memctrl.h"
#include "imc/imc.h"
#include "imc/loongson/loongson_la.h"

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
      case IMC_LA464:
      case IMC_LA664:
        get_imc_config_loongson_ddr4();
        break;
      default:
        break;
    }
}

void memctrl_poll_ecc(void)
{
    if (!ecc_status.ecc_enabled) {
        return;
    }
}
