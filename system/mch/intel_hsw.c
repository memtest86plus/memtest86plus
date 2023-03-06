// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Haswell CPUs (HSW)
//

#include "cpuinfo.h"
#include "memctrl.h"

#include "mch.h"

void get_imc_config_intel_hsw(void)
{
    imc_status.freq     = 1333;
    imc_status.tCL      = 16;
    imc_status.tCL_dec  = 16;
    imc_status.tRCD     = 10;
    imc_status.tRP      = 11;
    imc_status.tRAS     = 12;
}
