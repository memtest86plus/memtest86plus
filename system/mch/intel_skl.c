// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Skylake CPUs (SKL)
//

#include "cpuinfo.h"
#include "memctrl.h"

#include "mch.h"

void get_imc_config_intel_skl(void)
{
    imc.type    = "DDR4";
    imc.freq    = 1333;
    imc.width   = 64;
    imc.tCL     = 16;
    imc.tCL_dec = 16;
    imc.tRCD    = 10;
    imc.tRP     = 10;
    imc.tRAS    = 10;
}
