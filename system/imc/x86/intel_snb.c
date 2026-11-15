// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Sandy Bridge CPUs (SNB)
//

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "imc.h"

#define SNB_MMR_BASE_REG    0x48
#define SNB_REG_MAIN_CHAN0  0x5004
#define SNB_REG_MAIN_CHAN1  0x5008
#define SNB_REG_MCH_CFG     0x5E04
#define SNB_REG_TIMING      0x4000

void get_imc_config_intel_snb(void)
{
    uint32_t mmio_reg, offset;
    uint32_t mch_cfg, reg0, reg1;
    float cpu_ratio, dram_ratio;
    uint32_t *ptr;

    imc.type    = "DDR3";
    imc.tCL_dec = 0;

    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    mmio_reg = pci_config_read32(0, 0, 0, SNB_MMR_BASE_REG);

    if (!(mmio_reg & 0x1)) {
        pci_config_write32( 0, 0, 0, SNB_MMR_BASE_REG, mmio_reg | 1);
        mmio_reg = pci_config_read32(0, 0, 0, SNB_MMR_BASE_REG);
        if (!(mmio_reg & 0x1)) return;
    }
    mmio_reg &= 0xFFFFC000;

    // Get DRAM Ratio
    ptr = (uint32_t*)((uintptr_t)mmio_reg + SNB_REG_MCH_CFG);
    mch_cfg = *ptr & 0xFFFF;

    if ((mch_cfg >> 8) & 1) {
        dram_ratio = (float)(*ptr & 0x1F) * (100.0f / 100.0f);
    } else {
        dram_ratio = (float)(*ptr & 0x1F) * (133.34f / 100.0f);
    }

    // Get CPU Ratio
    rdmsr(MSR_IA32_PLATFORM_INFO, reg0, reg1);
    cpu_ratio = (float)((reg0 >> 8) & 0xFF);

    if (!cpu_ratio) return;

    // Compute DRAM Frequency
    imc.freq = ((clks_per_msec / 1000) / cpu_ratio) * dram_ratio * 2;

    if (imc.freq < 350 || imc.freq > 5000) {
        imc.freq = 0;
        return;
    }

    // Get Main Memory Controller Register for both channels
    ptr = (uint32_t*)((uintptr_t)mmio_reg + SNB_REG_MAIN_CHAN0);
    reg0 = *ptr & 0xFFFF;

    ptr = (uint32_t*)((uintptr_t)mmio_reg + SNB_REG_MAIN_CHAN1);
    reg1 = *ptr & 0xFFFF;

    // Populate IMC width
    imc.width = (reg0 && reg1) ? 128 : 64;

    // Define offset (chan A or B used)
    offset = reg0 ? 0x0 : 0x0400;

    // Get Main timing register
    reg0 = *(uint32_t*)((uintptr_t)mmio_reg + offset + SNB_REG_TIMING);

    // CAS Latency (tCAS)
    imc.tCL = (reg0 >> 8) & 0xF;

    // RAS-To-CAS (tRCD)
    imc.tRCD = reg0 & 0xF;

    // RAS Precharge (tRP)
    imc.tRP = (reg0 >> 4) & 0xF;

    // RAS Active to precharge (tRAS)
    imc.tRAS = (reg0 >> 16) & 0xFF;
}
