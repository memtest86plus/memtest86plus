// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Haswell CPUs (HSW)
//

#define HSW_MMR_BASE_REG    0x48
#define HSW_REG_MAIN_CHAN0  0x5004
#define HSW_REG_MAIN_CHAN1  0x5008
#define HSW_REG_MCH_CFG     0x5E04
#define HSW_REG_TIMING_CAS  0x4014
#define HSW_REG_TIMING_RCD  0x4000

static /*__attribute__((noinline))*/ void get_imc_config_intel_hsw(void)
{
    uint32_t mmio_reg, mch_cfg, offset;
    uint32_t reg0, reg1;
    float cpu_ratio, dram_ratio;
    uintptr_t *ptr;

    imc.type    = "DDR3";
    imc.tCL_dec = 0;

    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    mmio_reg = pci_config_read32(0, 0, 0, HSW_MMR_BASE_REG);
    if (!(mmio_reg & 0x1)) {
        pci_config_write32( 0, 0, 0, HSW_MMR_BASE_REG, mmio_reg | 1);
        mmio_reg = pci_config_read32(0, 0, 0, HSW_MMR_BASE_REG);
        if (!(mmio_reg & 0x1)) return;
    }
    mmio_reg &= 0xFFFFC000;

    // Get DRAM Ratio
    ptr = (uintptr_t*)((uintptr_t)mmio_reg + HSW_REG_MCH_CFG);
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
    ptr = (uintptr_t*)((uintptr_t)mmio_reg + HSW_REG_MAIN_CHAN0);
    reg0 = *ptr & 0xFFFF;

    ptr = (uintptr_t*)((uintptr_t)mmio_reg + HSW_REG_MAIN_CHAN1);
    reg1 = *ptr & 0xFFFF;

    // Populate IMC width
    imc.width = (reg0 && reg1) ? 128 : 64;

    // Define offset (ie: which channel is really used)
    offset = reg0 ? 0x0000 : 0x4000;

    // CAS Latency (tCAS)
    ptr = (uintptr_t*)((uintptr_t)mmio_reg + offset + HSW_REG_TIMING_CAS);
    imc.tCL = *ptr & 0x1F;

    // RAS-To-CAS (tRCD)
    ptr = (uintptr_t*)((uintptr_t)mmio_reg + offset + HSW_REG_TIMING_RCD);
    imc.tRCD = *ptr & 0x1F;

    // RAS Precharge (tRP)
    imc.tRP = (*ptr >> 5) & 0x1F;

    // RAS Active to precharge (tRAS)
    imc.tRAS = (*ptr >> 10) & 0x3F;
}
