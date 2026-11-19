// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Westmere (WMR) CPUs with integrated graphics:
// Clarkdale, Arrandale, Havendale (unreleased) and Auburndale (unreleased).
//
// Their memory controller is in the host bridge (MCHBAR), not on the uncore bus as on the
// other Nehalem / Westmere CPUs. Register usage comes from Memtest86+ v5
//

#include <stdbool.h>
#include <stdint.h>

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "imc.h"

#define WMR_MMR_BASE_REG    0x48
#define WMR_CH1_OFFSET      0x400   // channel 1 registers
#define WMR_REG_MRC         0x240   // [3:0] CL - 3, 0 if not set
#define WMR_REG_ACT         0x252   // [16:13] tRP
#define WMR_REG_READ        0x258   // [20:17] tRCD
#define WMR_REG_PRECHARGE   0x25D   // [5:0] tRAS
#define WMR_REG_CKE_CTRL    0x260   // [23:20] populated ranks
#define WMR_REG_ODT_CTRL    0x265   // [13:8] CL + 5
#define WMR_REG_CLK_RATIO   0x2C20  // [4:0] DRAM clock / BCLK, in quarters

// Several of these registers start at odd addresses.
static uint32_t mch_read32(uintptr_t addr)
{
    volatile uint8_t *p = (volatile uint8_t *)addr;

    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

void get_imc_config_intel_wmr(void)
{
    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    uint32_t mmio_reg = pci_config_read32(0, 0, 0, WMR_MMR_BASE_REG);

    if (!(mmio_reg & 0x1)) {
        pci_config_write32(0, 0, 0, WMR_MMR_BASE_REG, mmio_reg | 1);
        mmio_reg = pci_config_read32(0, 0, 0, WMR_MMR_BASE_REG);
        if (!(mmio_reg & 0x1)) return;
    }
    uintptr_t mch = mmio_reg & 0xFFFFC000;

    bool ch0 = (mch_read32(mch + WMR_REG_CKE_CTRL) >> 20) & 0xF;
    bool ch1 = (mch_read32(mch + WMR_CH1_OFFSET + WMR_REG_CKE_CTRL) >> 20) & 0xF;
    if (!ch0 && !ch1) return;

    // BCLK is the TSC rate divided by the maximum non-turbo ratio.
    uint32_t low, high;
    rdmsr(MSR_IA32_PLATFORM_INFO, low, high);
    unsigned int cpu_ratio = (low >> 8) & 0xFF;
    unsigned int dram_ratio = mch_read32(mch + WMR_REG_CLK_RATIO) & 0x1F;
    if (!cpu_ratio || !clks_per_msec) return;
    float freq = (float)clks_per_msec / 1000.0f / cpu_ratio * dram_ratio / 2.0f;
    if (freq < 350.0f || freq > 4000.0f) return;

    // Timings of the first populated channel
    uintptr_t ch = mch + (ch0 ? 0 : WMR_CH1_OFFSET);
    unsigned int cl = mch_read32(ch + WMR_REG_MRC) & 0xF;
    unsigned int odt = (mch_read32(ch + WMR_REG_ODT_CTRL) >> 8) & 0x3F;

    imc.type    = "DDR3";
    imc.width   = (ch0 && ch1) ? 128 : 64;
    imc.tCL_dec = 0;
    imc.tCL     = cl ? cl + 3 : (odt > 5 ? odt - 5 : 0);
    imc.tRCD    = (mch_read32(ch + WMR_REG_READ) >> 17) & 0xF;
    imc.tRP     = (mch_read32(ch + WMR_REG_ACT) >> 13) & 0xF;
    imc.tRAS    = mch_read32(ch + WMR_REG_PRECHARGE) & 0x3F;
    imc.freq    = freq + 0.5f;
}
