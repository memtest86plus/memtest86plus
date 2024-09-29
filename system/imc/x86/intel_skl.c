// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Skylake CPUs (SKL)
//

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"
#include "vmem.h"

#include "imc.h"

#define SKL_MMR_BASE_REG_LOW    0x48
#define SKL_MMR_BASE_REG_HIGH   0x4C
#define SKL_MMR_TIMINGS         0x4000
#define SKL_MMR_SCHEDULER_CONF  0x401C
#define SKL_MMR_TIMING_CAS      0x4070
#define SKL_MMR_MAD_CHAN0       0x500C
#define SKL_MMR_MAD_CHAN1       0x5010
#define SKL_MMR_DRAM_CLOCK      0x5E00

#define SKL_MMR_WINDOW_RANGE    (1UL << 15)

#define SKL_MMR_BASE_MASK       0x7FFFFF8000
#define SKL_MMR_MAD_IN_USE_MASK 0x003F003F

void get_imc_config_intel_skl(void)
{
    uint64_t mmio_reg;
    uint32_t reg0, reg1, offset;
    float cpu_ratio, dram_ratio;
    uintptr_t *ptr;

    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    mmio_reg = pci_config_read32(0, 0, 0, SKL_MMR_BASE_REG_LOW);
    if (!(mmio_reg & 0x1)) {
        pci_config_write32( 0, 0, 0, SKL_MMR_BASE_REG_LOW, mmio_reg | 1);
        mmio_reg = pci_config_read32(0, 0, 0, SKL_MMR_BASE_REG_LOW);
        if (!(mmio_reg & 0x1)) return;
    }

    mmio_reg |= (uint64_t)pci_config_read32(0, 0, 0, SKL_MMR_BASE_REG_HIGH) << 32;
    mmio_reg &= SKL_MMR_BASE_MASK;

#ifndef __x86_64__
    if (mmio_reg >= (1ULL << 32)) return;    // MMIO is outside reachable range
#endif

    uintptr_t mchbar_addr = map_region(mmio_reg, SKL_MMR_WINDOW_RANGE, false);

    // Get DRAM Ratio
    ptr = (uintptr_t*)(mchbar_addr + SKL_MMR_DRAM_CLOCK);
    reg0 = *ptr & 0xF;

    if (reg0 < 3) return;

    dram_ratio = reg0 * (133.34f / 100.0f);

    // Get CPU Ratio
    rdmsr(MSR_IA32_PLATFORM_INFO, reg0, reg1);
    cpu_ratio = (float)((reg0 >> 8) & 0xFF);

    if (!cpu_ratio) return;

    // Compute DRAM Frequency
    imc.freq = ((clks_per_msec / 1000) / cpu_ratio) * dram_ratio * 2;

    if (imc.freq < 150 || imc.freq > 8000) {
        imc.freq = 0;
        return;
    }

    // Get Main Memory Controller Register for both channels
    ptr = (uintptr_t*)(mchbar_addr + SKL_MMR_MAD_CHAN0);
    reg0 = *ptr & SKL_MMR_MAD_IN_USE_MASK;

    ptr = (uintptr_t*)(mchbar_addr + SKL_MMR_MAD_CHAN1);
    reg1 = *ptr & SKL_MMR_MAD_IN_USE_MASK;

    // Populate IMC width
    imc.width = (reg0 && reg1) ? 128 : 64;

    // Define offset (ie: which channel is really used)
    offset = reg0 ? 0x0000 : 0x0400;

    // SKL supports DDR3 & DDR4. Check DDR Type.
    ptr = (uintptr_t*)(mchbar_addr + offset + SKL_MMR_SCHEDULER_CONF);
    imc.type = (*ptr & 0x3) ? "DDR3" : "DDR4";

    // CAS Latency (tCAS)
    ptr = (uintptr_t*)(mchbar_addr + offset + SKL_MMR_TIMING_CAS);
    imc.tCL = (*ptr >> 16) & 0x1F;
    imc.tCL_dec = 0;

    // RAS-To-CAS (tRCD) & RAS Precharge (tRP)
    ptr = (uintptr_t*)(mchbar_addr + offset + SKL_MMR_TIMINGS);
    imc.tRP = imc.tRCD = *ptr & 0x3F;

    // RAS Active to precharge (tRAS)
    imc.tRAS = (*ptr >> 8) & 0x7F;
}
