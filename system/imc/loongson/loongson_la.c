// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
//
// Platform-specific code for Loongson LoongArch CPU
//

#include "boot.h"
#include "cpuinfo.h"
#include "memctrl.h"
#include "memrw.h"
#include "cache.h"
#include "cpuid.h"

#include <larchintrin.h>

#include "../imc.h"

#define ENV_SIZE 0x100000 // 1MB

#define MC_CONF_ADDRESS   0x800000000FF00000ULL
#define CHIP_CONF_ADDRESS 0x800000001FE00000ULL

bool route_flag;
uint8_t max_mc;

void read_imc_sequence(void)
{
    imc.tCL     = (uint16_t)read8((uint8_t *)(MC_CONF_ADDRESS + 0x1060));
    imc.tCL_dec = 0;
    imc.tRP     = (uint16_t)read8((uint8_t *)(MC_CONF_ADDRESS + 0x1006));
    imc.tRCD    = (uint16_t)read8((uint8_t *)(MC_CONF_ADDRESS + 0x1047));
    imc.tRAS    = (uint16_t)read8((uint8_t *)(MC_CONF_ADDRESS + 0x1040));

    switch ((read8((uint8_t *)(MC_CONF_ADDRESS + 0x1024)) & 0x7)) {
        case 0:
              imc.width = 64;
              break;
        case 3:
              imc.width = 16;
              break;
        case 5:
              imc.width = 32;
              break;
        default:
              imc.width = 0;
              break;

    }
}

bool read_imc_info(void)
{
    uint64_t fun_val;
    uint8_t  i;
    bool     ret = false;

    if (route_flag) {
        for (i = 0; i < max_mc; i++) {
            fun_val  = read64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180));
            write64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180), (fun_val & (~(1 << 4))));

            if (read8((uint8_t *)(MC_CONF_ADDRESS)) == 0xFF || read8((uint8_t *)(MC_CONF_ADDRESS)) == 0x00) {
                write64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180), fun_val);
                continue;
            }

            read_imc_sequence();

            write64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180), fun_val);
            ret = true;
            break;
        }
    } else {
        for (i = 0; i < max_mc; i++) {
            fun_val  = read64((uint64_t *)(CHIP_CONF_ADDRESS | 0x180));
            write64((uint64_t *)(CHIP_CONF_ADDRESS | 0x180), (fun_val & (~(1 << (4 + (i * 5))))));

            if (read8((uint8_t *)(MC_CONF_ADDRESS)) == 0xFF || read8((uint8_t *)(MC_CONF_ADDRESS)) == 0x00) {
                write64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180), fun_val);
                continue;
            }

            read_imc_sequence();

            write64((uint64_t *)(CHIP_CONF_ADDRESS | 0x180), fun_val);
            ret = true;
            break;
        }
    }

    return ret;
}

void get_imc_config_loongson_ddr4(void)
{
    uint32_t val;
    uint16_t refc, loopc, div, div_mode, ref_clk;

    imc.type  = "DDR4";

    if (strstr(cpuid_info.brand_id.str, "3C") ||
        (strstr(cpuid_info.brand_id.str, "3B6000") &&
         !strstr(cpuid_info.brand_id.str, "3B6000M"))) {
        route_flag = true;
        max_mc     = 4;
    } else if (strstr(cpuid_info.brand_id.str, "3D") ||
        strstr(cpuid_info.brand_id.str, "3E")) {
        route_flag = true;
        max_mc     = 8;
    } else if (strstr(cpuid_info.brand_id.str, "3A") ||
                strstr(cpuid_info.brand_id.str, "3B")) {
        route_flag = false;
        max_mc     = 2;
    } else if (strstr(cpuid_info.brand_id.str, "2K") ||
                strstr(cpuid_info.brand_id.str, "3B6000M")) {
        route_flag = false;
        max_mc     = 1;
    }

    if (read_imc_info()) {
        val = __iocsrrd_w(0x1c0);

        loopc    = (val >> 14) & 0x3FF;
        div      = (val >> 24) & 0x3F;
        div_mode = 0x1 << ((val >> 4) & 0x3);
        refc     = (val >> 8) & 0x1f;
        ref_clk  = (uint16_t)(((__cpucfg(0x4) * (__cpucfg(0x5) & 0xFFFF)) / ((__cpucfg(0x5) >> 16) & 0xFFFF)) / 1000000);
        imc.freq = (ref_clk * loopc / refc / div / div_mode) * 4;
    } else {
        imc.freq = 0;
    }
}
