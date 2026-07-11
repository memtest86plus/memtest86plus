// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester
//
// SPD/I2C access is not supported yet on ARM64.

#include <stdbool.h>
#include <stdint.h>

#include "i2c_x86.h"

uint8_t get_spd(uint8_t slot_idx __attribute__((unused)), uint16_t spd_adr __attribute__((unused)))
{
    return 0;
}

int print_spd_startup_info(void)
{
    // No SPD access on ARM64; return 0 so the caller uses the SMBIOS Type 17 info.
    return 0;
}
