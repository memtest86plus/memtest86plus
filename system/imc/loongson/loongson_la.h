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

#define MC_CONF_ADDRESS   0x800000000FF00000ULL
#define CHIP_CONF_ADDRESS 0x800000001FE00000ULL

static void read_imc_sequence(void)
{
    imc.tCL     = (uint16_t)read8((uint8_t *)(MC_CONF_ADDRESS + 0x1060));
    imc.tCL_dec = 0;
    imc.tRP     = (uint16_t)read8((uint8_t *)(MC_CONF_ADDRESS + 0x1006));
    imc.tRCD    = (uint16_t)read8((uint8_t *)(MC_CONF_ADDRESS + 0x1047));
    imc.tRAS    = (uint16_t)read8((uint8_t *)(MC_CONF_ADDRESS + 0x1040));
}

static bool read_imc_info(uint8_t node_num, uint8_t max_mc, bool route_flag)
{
    uint64_t fun_val;
    uint8_t  i, j;
    uint8_t  active_mc_num = 0;
    bool     ret = false;

    if (!max_mc) {
        return ret;
    }

    if (route_flag) {
        for (i = 0; i < max_mc; i++) {
            fun_val  = read64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180));
            write64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180), (fun_val & (~(1 << 4))));

            if (read8((uint8_t *)(MC_CONF_ADDRESS)) == 0xFF || read8((uint8_t *)(MC_CONF_ADDRESS)) == 0x00) {
                write64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180), fun_val);
                continue;
            }

            read_imc_sequence();
            imc.width = 64;

            write64((uint64_t *)(CHIP_CONF_ADDRESS | i << 16 | 0x180), fun_val);
            ret = true;
            break;
        }

        for (j = 0; j < node_num; j++) {
            i = read8((uint8_t *)(CHIP_CONF_ADDRESS | 0x411 | ((uint64_t)j << 44)));
            if ((i & 0x3) == ((i >> 2) & 0x3)) {
                active_mc_num += 1;
            } else {
                if ((i & 0x3) == ((i >> 4) & 0x3)) {
                    active_mc_num += 2;
                } else {
                    active_mc_num += 4;
                }
            }
        }
    } else {
        for (i = 0; i < max_mc; i++) {
            fun_val  = read64((uint64_t *)(CHIP_CONF_ADDRESS | 0x180));
            write64((uint64_t *)(CHIP_CONF_ADDRESS | 0x180), (fun_val & (~(1 << (4 + (i * 5))))));

            if (read8((uint8_t *)(MC_CONF_ADDRESS)) == 0xFF || read8((uint8_t *)(MC_CONF_ADDRESS)) == 0x00) {
                write64((uint64_t *)(CHIP_CONF_ADDRESS | 0x180), fun_val);
                continue;
            }

            read_imc_sequence();

            write64((uint64_t *)(CHIP_CONF_ADDRESS | 0x180), fun_val);
            ret = true;
            break;
        }

        switch ((read8((uint8_t *)(MC_CONF_ADDRESS + 0x1203)) & 0x3)) {
            case 1:
                  imc.width = 16;
                  break;
            case 2:
                  imc.width = 32;
                  break;
            case 3:
                  imc.width = 64;
                  break;
            default:
                  imc.width = 0;
                  break;
        }

        switch (((read64((uint64_t *)(CHIP_CONF_ADDRESS | 0x400))) >> 30) & 0x3) {
            case 1:
            case 2:
                  active_mc_num = 1;
                  break;
            case 3:
                  active_mc_num = 2;
                  break;
            default:
                  active_mc_num = 0;
                  break;
        }
    }

    imc.width *= active_mc_num;

    return ret;
}

static void /*__attribute__((noinline))*/ get_imc_config_loongson_ddr4(void)
{
    uint32_t val;
    uint16_t refc, loopc, div, div_mode, ref_clk;
    uint8_t  max_mc, node_num;
    bool route_flag;

    imc.type  = "DDR4";
    node_num  = 1;

    if (strstr(cpuid_info.brand_id.str, "3C") ||
        (strstr(cpuid_info.brand_id.str, "3B6000") &&
         !strstr(cpuid_info.brand_id.str, "3B6000M"))) {
        route_flag = true;
        max_mc     = 4;
    } else if (strstr(cpuid_info.brand_id.str, "3D")) {
        route_flag = true;
        max_mc     = 8;
        node_num   = 2;
    } else if (strstr(cpuid_info.brand_id.str, "3E")) {
        route_flag = true;
        max_mc     = 8;
        node_num   = 4;
    } else if (strstr(cpuid_info.brand_id.str, "3A") ||
                strstr(cpuid_info.brand_id.str, "3B")) {
        route_flag = false;
        max_mc     = 2;
    } else if (strstr(cpuid_info.brand_id.str, "2K") ||
                strstr(cpuid_info.brand_id.str, "3B6000M")) {
        route_flag = false;
        max_mc     = 1;
    } else {
        route_flag = false;
        max_mc     = 0;
        node_num   = 0;
    }

    if (read_imc_info(node_num, max_mc, route_flag)) {
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
