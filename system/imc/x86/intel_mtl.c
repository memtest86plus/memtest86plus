// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2024 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Meteor Lake (MTL) and
// Arrow-Lake (ARL) CPUs
//

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"
#include "vmem.h"

#include "imc.h"

#define MTL_MMR_BASE_REG_LOW    0x48
#define MTL_MMR_BASE_REG_HIGH   0x4C

#define MTL_MMR_WINDOW_RANGE    (1UL << 17)
#define MTL_MMR_BASE_MASK       0x3FFFFFE0000

#define MTL_MMR_MC1_OFFSET      0x10000
#define MTL_MMR_CH1_OFFSET      0x800

#define MTL_MMR_IC_DECODE       0xD800
#define MTL_MMR_CH0_DIMM_REG    0xD80C

#define MTL_MMR_CH0_PRE_REG     0xE000
#define MTL_MMR_CH0_CAS_REG     0xE070
#define MTL_MMR_CH0_ACT_REG     0xE138

#define MTL_MMR_PTGRAM_REG      0x13D98

void get_imc_config_intel_mtl(void)
{
    uint64_t mmio_reg;
    uint32_t cha, chb, offset;
    uint32_t tmp;
    uintptr_t *ptr;
    uint32_t *ptr32;

    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    mmio_reg = pci_config_read32(0, 0, 0, MTL_MMR_BASE_REG_LOW);
    if (!(mmio_reg & 0x1)) {
        pci_config_write32( 0, 0, 0, MTL_MMR_BASE_REG_LOW, mmio_reg | 1);
        mmio_reg = pci_config_read32(0, 0, 0, MTL_MMR_BASE_REG_LOW);
        if (!(mmio_reg & 0x1)) return;
    }

    mmio_reg |= (uint64_t)pci_config_read32(0, 0, 0, MTL_MMR_BASE_REG_HIGH) << 32;
    mmio_reg &= MTL_MMR_BASE_MASK;

#ifndef __x86_64__
    if (mmio_reg >= (1ULL << 32)) return;    // MMIO is outside reachable range (> 32bit)
#endif

    uintptr_t mchbar_addr = map_region(mmio_reg, MTL_MMR_WINDOW_RANGE, false);

    // Get channel configuration & IMC width
    cha = *(uintptr_t*)(mchbar_addr + MTL_MMR_CH0_DIMM_REG);
    tmp = *(uintptr_t*)(mchbar_addr + MTL_MMR_IC_DECODE);
    cha = ~cha ? 1 << (((tmp >> 27) & 3) + 4) : 0;

    chb = *(uintptr_t*)(mchbar_addr + MTL_MMR_CH0_DIMM_REG + MTL_MMR_MC1_OFFSET);
    tmp = *(uintptr_t*)(mchbar_addr + MTL_MMR_IC_DECODE + MTL_MMR_MC1_OFFSET);
    chb = ~chb ? 1 << (((tmp >> 27) & 3) + 4) : 0;

    offset = cha ? 0x0 : MTL_MMR_MC1_OFFSET;
    imc.width = (cha + chb) * 2;

    // MTL+ only supports DDR5
    imc.type = "DDR5";

    // Get Memory Clock
    ptr32 = (uint32_t*)(mchbar_addr + MTL_MMR_PTGRAM_REG);

    switch((*ptr32 >> 20) & 0xF) {
        default:
        case 0x1:
            imc.freq = 200;
            break;
        case 0x2:
            imc.freq = 100;
            break;
        case 0xA:
            imc.freq = 133;
            break;
        case 0xB:
            imc.freq = 66;
            break;
        case 0xC:
            imc.freq = 33;
            break;
    }

    imc.freq *= (*ptr32 >> 12) & 0xFF; // * Divider
    imc.freq *= (((*ptr32 >> 24) & 1) + 1) * 2; // * Gear * DDR

    // Get DRAM Timings
    ptr = (uintptr_t*)(mchbar_addr + offset + MTL_MMR_CH0_CAS_REG);
    imc.tCL = (*ptr >> 16) & 0x7F;

    ptr = (uintptr_t*)(mchbar_addr + offset + MTL_MMR_CH0_ACT_REG);
    imc.tRCD = (*ptr >> 22) & 0xFF;

    ptr = (uintptr_t*)(mchbar_addr + offset + MTL_MMR_CH0_PRE_REG);
    imc.tRP = (*ptr >> 10) & 0xFF;

    ptr32 = (uint32_t*)((uintptr_t)mchbar_addr + offset + MTL_MMR_CH0_PRE_REG + 4);
    imc.tRAS = (*ptr32 >> 13) & 0x1FF;
}
