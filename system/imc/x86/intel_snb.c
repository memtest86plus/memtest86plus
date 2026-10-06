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
#include "tsc.h"
#include "unistd.h"
#include "vmem.h"

#include "imc.h"

#define PCI_DID_SNB_EP_TARGET_ADDRESS_DECODER 0x3CAA
#define PCI_DID_IVB_EP_TARGET_ADDRESS_DECODER 0x0EAA
#define PCI_DID_IVB_EP_TARGET_ADDRESS_REGISTERS 0x0E68
#define PCI_DID_IVB_EP_TARGET_ADDRESS_DECODER_REGISTERS 0x0E6A

#define SNB_MMR_BASE_REG    0x48
#define SNB_REG_MAIN_CHAN0  0x5004
#define SNB_REG_MAIN_CHAN1  0x5008
#define SNB_REG_REF_CLOCK   0x5E00
#define SNB_REG_MCH_CFG     0x5E04
#define SNB_REG_TIMING      0x4000

// EP register references and channel numbering: 327043-001, 331051-002, Linux arch/x86/events/intel/uncore_snbep.c .
static bool read_dram_clocks_pci(int bus, int dev, int func, uint64_t *clocks)
{
    uint32_t high, low, next;
    for (unsigned int i = 0; i < 3; i++) {
        high = pci_config_read32(bus, dev, func, 0xD4);
        low  = pci_config_read32(bus, dev, func, 0xD0);
        next = pci_config_read32(bus, dev, func, 0xD4);
        if (high == next) {
            *clocks = ((uint64_t)(high & 0xFFFF) << 32) | low;
            return true;
        }
    }
    return false;
}

uint16_t get_imc_freq_intel_ep(int bus, int dev, int func)
{
    uint32_t ctl = pci_config_read32(bus, dev, func, 0xF0);
    uint32_t box = pci_config_read32(bus, dev, func, 0xF4);
    if (!clks_per_msec || (box & 3)) return 0;

    // Enable the fixed DCLK counter without resetting it or enabling overflow interrupts.
    // Box freeze is write-only. SNB requires bit 17 clear; IVB/HSW require 17:16 set (Linux drivers/edac/sb_edac.c).
    pci_config_write32(bus, dev, func, 0xF0, 1 << 22);
    pci_config_write32(bus, dev, func, 0xF4, imc.family == IMC_SNB_E ? 0 : 3 << 16);
    uint64_t start, end;
    bool valid = read_dram_clocks_pci(bus, dev, func, &start);
    uint64_t tsc = get_tsc();
    usleep(10000);
    valid = read_dram_clocks_pci(bus, dev, func, &end) && valid;
    tsc = get_tsc() - tsc;
    // Restore readable fixed-counter controls, without replaying a reset command.
    pci_config_write32(bus, dev, func, 0xF0, ctl & ~(1 << 19));
    pci_config_write32(bus, dev, func, 0xF4, box);

    if (!valid || !tsc) return 0;
    uint64_t clocks = (end - start) & 0xFFFFFFFFFFFF;
    float freq = (float)clocks * clks_per_msec / (float)tsc / 500.0f;
    return (freq >= 350 && freq <= 5000) ? freq + 0.5f : 0;
}

void get_imc_config_intel_snb_ep(void)
{
    uint16_t tad_id = imc.family == IMC_SNB_E ? PCI_DID_SNB_EP_TARGET_ADDRESS_DECODER : PCI_DID_IVB_EP_TARGET_ADDRESS_DECODER;
    unsigned int channels = 0;

    for (int bus = 0; bus < PCI_MAX_BUS && !channels; bus++) {
        if (pci_config_read32(bus, 15, 2, 0) != ((uint32_t)tad_id << 16 | PCI_VID_INTEL)) continue;
        // Xeon E7 v2 / Xeon E5 v2 IMC has two-controller arrangement: Linux drivers/edac/sb_edac.c .
        bool dual = imc.family == IMC_IVB_E && pci_config_read32(bus, 29, 0, 0) == (((uint32_t)PCI_DID_IVB_EP_TARGET_ADDRESS_REGISTERS << 16) | PCI_VID_INTEL);
        uint32_t timing = 0;
        int first_dev = -1, first_func = 0;
        for (int mc = 0; mc < (dual ? 2 : 1); mc++) {
            int tad_dev = mc ? 29 : 15;
            int timing_dev = mc ? 30 : 16;
            uint16_t id = mc ? PCI_DID_IVB_EP_TARGET_ADDRESS_DECODER_REGISTERS : tad_id;
            for (int ch = 0; ch < (dual ? 2 : 4); ch++) {
                if (pci_config_read32(bus, tad_dev, ch + 2, 0) == ((uint32_t)(id + ch) << 16 | PCI_VID_INTEL)) {
                    static const int timing_func[4] = {4, 5, 0, 1};
                    bool populated = false;
                    for (int slot = 0; slot < 3; slot++) {
                        uint32_t mtr = pci_config_read32(bus, tad_dev, ch + 2, 0x80 + 4 * slot);
                        if (mtr & (1 << 14)) populated = true;
                    }
                    if (populated) {
                        uintptr_t addr = pci_config_mmio_addr(bus, timing_dev, timing_func[ch]);
                        if (!addr || (*(volatile uint32_t *)addr & 0xFFFF) != PCI_VID_INTEL) return;
                        uint32_t reg = *(volatile uint32_t *)(addr + 0x200);
                        channels++;
                        if (first_dev < 0) {
                            timing = reg;
                            first_dev = timing_dev;
                            first_func = timing_func[ch];
                        }
                    }
                }
            }
        }
        if (channels) {
            imc.type    = "DDR3";
            imc.width   = channels * 64;
            imc.tCL_dec = 0;
            imc.tCL     = (timing >> 9) & 0x1F;
            imc.tRCD    = timing & 0x1F;
            imc.tRP     = (timing >> 5) & 0xF;
            imc.tRAS    = (timing >> 19) & 0x3F;
            imc.freq    = get_imc_freq_intel_ep(bus, first_dev, first_func);
        }
    }
}

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
