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
#define SKL_MMR_MAD_INTER       0x5000
#define SKL_MMR_MAD_INTRA_CH0   0x5004
#define SKL_MMR_MAD_INTRA_CH1   0x5008
#define SKL_MMR_MAD_CHAN0       0x500C
#define SKL_MMR_MAD_CHAN1       0x5010
#define SKL_MMR_CHANNEL_HASH    0x5024
#define SKL_MMR_CHANNEL_EHASH   0x5028
#define ICL_MMR_REMAPBASE       0x5090
#define ICL_MMR_REMAPLIMIT      0x5098
#define SKL_MMR_SA_PERF_STATUS  0x5918
#define SKL_MMR_DRAM_CLOCK      0x5E00

#define SKL_MMR_WINDOW_RANGE    (1UL << 15)

#define SKL_MMR_BASE_MASK       0x7FFFFF8000
#define SKL_MMR_MAD_IN_USE_MASK 0x003F003F

#define SKL_PCI_REMAPBASE       0x90
#define SKL_PCI_REMAPLIMIT      0x98
#define SKL_PCI_TOM             0xA0
#define SKL_PCI_TOUUD           0xA8
#define SKL_PCI_TOLUD           0xBC

static uint32_t mch_read32(uintptr_t mchbar_addr, uint32_t offset)
{
    return *(volatile uint32_t *)(mchbar_addr + offset);
}

static uint64_t mch_read64(uintptr_t mchbar_addr, uint32_t offset)
{
    return mch_read32(mchbar_addr, offset) | (uint64_t)mch_read32(mchbar_addr, offset + 4) << 32;
}

static uint64_t pci_read64(int reg)
{
    return pci_config_read32(0, 0, 0, reg) | (uint64_t)pci_config_read32(0, 0, 0, reg + 4) << 32;
}

// DIMM decoder registers (datasheets vol. 2: 332688 / 337345 for SKL, 636761 for RKL). On SKL the
// hash registers are not documented: read at their RKL offsets, the decoder uses them in hash mode only.
void get_mad_intel(uintptr_t mchbar_addr, intel_mad_layout_t layout)
{
    intel_mad.layout       = layout;
    intel_mad.mad_inter    = mch_read32(mchbar_addr, SKL_MMR_MAD_INTER);
    intel_mad.mad_intra[0] = mch_read32(mchbar_addr, SKL_MMR_MAD_INTRA_CH0);
    intel_mad.mad_intra[1] = mch_read32(mchbar_addr, SKL_MMR_MAD_INTRA_CH1);
    intel_mad.mad_dimm[0]  = mch_read32(mchbar_addr, SKL_MMR_MAD_CHAN0);
    intel_mad.mad_dimm[1]  = mch_read32(mchbar_addr, SKL_MMR_MAD_CHAN1);
    intel_mad.ch_hash      = mch_read32(mchbar_addr, SKL_MMR_CHANNEL_HASH);
    intel_mad.ch_ehash     = mch_read32(mchbar_addr, SKL_MMR_CHANNEL_EHASH);
    if (layout == INTEL_MAD_SKL) {
        intel_mad.remap_base  = pci_read64(SKL_PCI_REMAPBASE);
        intel_mad.remap_limit = pci_read64(SKL_PCI_REMAPLIMIT);
    } else {
        intel_mad.remap_base  = mch_read64(mchbar_addr, ICL_MMR_REMAPBASE);
        intel_mad.remap_limit = mch_read64(mchbar_addr, ICL_MMR_REMAPLIMIT);
    }
    intel_mad.tom          = pci_read64(SKL_PCI_TOM);
    intel_mad.touud        = pci_read64(SKL_PCI_TOUUD);
    intel_mad.tolud        = pci_config_read32(0, 0, 0, SKL_PCI_TOLUD);
    intel_mad.valid        = intel_mad_view(&intel_mad, &imc_view);
}

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

    get_mad_intel(mchbar_addr, INTEL_MAD_SKL);

    // Get DRAM Ratio (QCLK_RATIO, bit 7 = 100 MHz reference instead of 133 MHz)
    ptr = (uintptr_t*)(mchbar_addr + SKL_MMR_SA_PERF_STATUS);
    reg0 = *ptr & 0xFF;

    if ((reg0 & 0x7F) < 3) return;

    dram_ratio = (reg0 & 0x80) ? (reg0 & 0x7F) : (reg0 & 0x7F) * (133.34f / 100.0f);

    // Get CPU Ratio
    rdmsr(MSR_IA32_PLATFORM_INFO, reg0, reg1);
    cpu_ratio = (float)((reg0 >> 8) & 0xFF);

    if (!cpu_ratio) return;

    // Compute DRAM Frequency (QCLK ratio already gives the data rate)
    imc.freq = ((clks_per_msec / 1000) / cpu_ratio) * dram_ratio;

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
