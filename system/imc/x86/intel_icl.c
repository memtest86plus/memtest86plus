// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel IceLake CPUs (ICL)
//

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"
#include "vmem.h"

#include "imc.h"

#define ICL_MMR_BASE_REG_LOW    0x48
#define ICL_MMR_BASE_REG_HIGH   0x4C
#define ICL_MMR_TIMINGS         0x4000
#define ICL_MMR_TIMING_CAS      0x4070
#define ICL_MMR_MAD_CHAN0       0x500C
#define ICL_MMR_MAD_CHAN1       0x5010
#define ICL_MMR_DRAM_CLOCK      0x5E00

#define ICL_MMR_MC_BIOS_REG     0x5E04
#define ICL_MMR_BLCK_REG        0x5F60

#define ICL_MMR_WINDOW_RANGE    (1UL << 15)

#define ICL_MMR_BASE_MASK       0x7FFFFF8000
#define ICL_MMR_MAD_IN_USE_MASK 0x003F003F

void get_imc_config_intel_icl(void)
{
    uint64_t mmio_reg;
    uint32_t reg0, reg1, offset;
    float bclk;
    uintptr_t *ptr;

    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    mmio_reg = pci_config_read32(0, 0, 0, ICL_MMR_BASE_REG_LOW);
    if (!(mmio_reg & 0x1)) {
        pci_config_write32( 0, 0, 0, ICL_MMR_BASE_REG_LOW, mmio_reg | 1);
        mmio_reg = pci_config_read32(0, 0, 0, ICL_MMR_BASE_REG_LOW);
        if (!(mmio_reg & 0x1)) return;
    }

    mmio_reg |= (uint64_t)pci_config_read32(0, 0, 0, ICL_MMR_BASE_REG_HIGH) << 32;
    mmio_reg &= ICL_MMR_BASE_MASK;

#ifndef __x86_64__
    if (mmio_reg >= (1ULL << 32)) return;    // MMIO is outside reachable range
#endif

    uintptr_t mchbar_addr = map_region(mmio_reg, ICL_MMR_WINDOW_RANGE, false);

    imc.type = "DDR4";

    // Get SoC Base Clock
    ptr = (uintptr_t*)(mchbar_addr + ICL_MMR_BLCK_REG);
    bclk = (*ptr & 0xFFFFFFFF) / 1000.0f;

    // Get Memory Clock (QClk), apply Gear & clock ratio
    ptr = (uintptr_t*)(mchbar_addr + ICL_MMR_MC_BIOS_REG);
    imc.freq = (*ptr & 0xFF) * bclk;

    if (*ptr & 0x10000) {
        imc.freq *= 2;
    }

    if ((*ptr & 0xF00) == 0) {
        imc.freq *= 133.34f / 100.0f;
    }

    // Get Main Memory Controller Register for both channels
    ptr = (uintptr_t*)(mchbar_addr + ICL_MMR_MAD_CHAN0);
    reg0 = *ptr & ICL_MMR_MAD_IN_USE_MASK;

    ptr = (uintptr_t*)(mchbar_addr + ICL_MMR_MAD_CHAN1);
    reg1 = *ptr & ICL_MMR_MAD_IN_USE_MASK;

    // Populate IMC width
    imc.width = (reg0 && reg1) ? 128 : 64;

    // Define offset (ie: which channel is really used)
    offset = reg0 ? 0x0000 : 0x0400;

    // CAS Latency (tCAS)
    ptr = (uintptr_t*)(mchbar_addr + offset + ICL_MMR_TIMING_CAS);
    imc.tCL = (*ptr >> 16) & 0x1F;
    imc.tCL_dec = 0;

    // RAS-To-CAS (tRCD) & RAS Precharge (tRP)
    ptr = (uintptr_t*)(mchbar_addr + offset + ICL_MMR_TIMINGS);
    imc.tRP = imc.tRCD = *ptr & 0x3F;

    // RAS Active to precharge (tRAS)
    imc.tRAS = (*ptr >> 9) & 0x7F;
}
