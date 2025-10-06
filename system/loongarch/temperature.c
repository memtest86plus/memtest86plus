// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.

#include <stdint.h>

#include "config.h"
#include "cpuid.h"
#include "cpuinfo.h"
#include "hwquirks.h"
#include "memctrl.h"

#include "temperature.h"

#include <larchintrin.h>

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

float cpu_temp_offset = 0;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void cpu_temp_init(void)
{
    if (!enable_temp_cpu) {
        return;
    }

    // Process temperature-related quirks
    if (quirk.type & QUIRK_TYPE_TEMP) {
        quirk.process();
    }
}

int get_cpu_temp(void)
{
    return (int)(__iocsrrd_w(0x428));
}

int get_ram_temp(uint8_t slot)
{
    return 0;
}
