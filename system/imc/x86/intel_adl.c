// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Alder Lake CPUs (ADL-S)
//

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"
#include "vmem.h"

#include "imc.h"

#define ADL_MMR_BASE_REG_LOW    0x48
#define ADL_MMR_BASE_REG_HIGH   0x4C

#define ADL_MMR_WINDOW_RANGE    (1UL << 17)
#define ADL_MMR_BASE_MASK       0x3FFFFFE0000

#define ADL_MMR_MC1_OFFSET      0x10000
#define ADL_MMR_CH1_OFFSET      0x800

#define ADL_MMR_IC_DECODE       0xD800
#define ADL_MMR_CH0_DIMM_REG    0xD80C
#define ADL_MMR_MC0_REG         0xE000
#define ADL_MMR_ODT_TCL_REG     0xE070
#define ADL_MMR_MC_INIT_REG     0xE454

#define ADL_MMR_SA_PERF_REG     0x5918
#define ADL_MMR_MC_BIOS_REG     0x5E04
#define ADL_MMR_BLCK_REG        0x5F60

void get_imc_config_intel_adl(void)
{
    uint64_t mmio_reg;
    uint32_t cha, chb, offset;
    float bclk;
    uintptr_t *ptr;
    uint32_t *ptr32;

    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    mmio_reg = pci_config_read32(0, 0, 0, ADL_MMR_BASE_REG_LOW);
    if (!(mmio_reg & 0x1)) {
        pci_config_write32( 0, 0, 0, ADL_MMR_BASE_REG_LOW, mmio_reg | 1);
        mmio_reg = pci_config_read32(0, 0, 0, ADL_MMR_BASE_REG_LOW);
        if (!(mmio_reg & 0x1)) return;
    }

    mmio_reg |= (uint64_t)pci_config_read32(0, 0, 0, ADL_MMR_BASE_REG_HIGH) << 32;
    mmio_reg &= ADL_MMR_BASE_MASK;

#ifndef __x86_64__
    if (mmio_reg >= (1ULL << 32)) return;    // MMIO is outside reachable range (> 32bit)
#endif

    uintptr_t mchbar_addr = map_region(mmio_reg, ADL_MMR_WINDOW_RANGE, false);

    // Get channel configuration & IMC width
    cha = *(uintptr_t*)(mchbar_addr + ADL_MMR_CH0_DIMM_REG);
    cha = ~cha ? (((cha >> 16) & 0x7F) + (cha & 0x7F)) : 0;

    chb = *(uintptr_t*)(mchbar_addr + ADL_MMR_CH0_DIMM_REG + ADL_MMR_MC1_OFFSET);
    chb = ~chb ? (((chb >> 16) & 0x7F) + (chb & 0x7F)) : 0;

    offset = cha ? 0x0 : ADL_MMR_MC1_OFFSET;
    imc.width = (cha && chb) ? 64 : 128;

    // Get Memory Type (ADL supports DDR4 & DDR5)
    cha = *(uintptr_t*)(mchbar_addr + offset + ADL_MMR_IC_DECODE) & 0x7;
    imc.type = (cha == 1 || cha == 2) ? "DDR5" : "DDR4";

    // Get SoC Base Clock
    ptr = (uintptr_t*)(mchbar_addr + ADL_MMR_BLCK_REG);
    bclk = (*ptr & 0xFFFFFFFF) / 1000.0f;

    // Get Memory Clock (QClk), apply Gear & clock ratio
    ptr = (uintptr_t*)(mchbar_addr + ADL_MMR_SA_PERF_REG);
    imc.freq = ((*ptr >> 2) & 0xFF) * bclk;

    ptr = (uintptr_t*)(mchbar_addr + ADL_MMR_MC_BIOS_REG);
    imc.freq <<= (*ptr >> 12) & 0x3;

    if ((*ptr & 0xF00) == 0) {
        imc.freq *= 133.34f / 100.0f;
    }

    // Get DRAM Timings
    ptr = (uintptr_t*)(mchbar_addr + offset + ADL_MMR_ODT_TCL_REG);
    imc.tCL = (*ptr >> 16) & 0x7F;

    ptr = (uintptr_t*)(mchbar_addr + offset + ADL_MMR_MC0_REG);
    imc.tRP = *ptr & 0xFF;

    ptr32 = (uint32_t*)((uintptr_t)mchbar_addr + offset + ADL_MMR_MC0_REG + 4);
    imc.tRAS = (*ptr32 >> 10) & 0x1FF;
    imc.tRCD = (*ptr32 >> 19) & 0xFF;
}
