// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester

#include <stdint.h>

#include "config.h"
#include "cpuid.h"
#include "cpuinfo.h"
#include "hwquirks.h"
#include "memctrl.h"

#include "temperature.h"

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

float cpu_temp_offset = 0;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void cpu_temp_init(void)
{
    // Temperature reporting not yet supported on ARM64.
}

int get_cpu_temp(void)
{
    return TEMP_INVALID;
}

int get_ram_temp(uint8_t slot __attribute__((unused)))
{
    return TEMP_INVALID;
}
