// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Haswell CPUs (HSW)
//

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"
#include "vmem.h"

#include "imc.h"

#define PCI_DID_HSW_EP_TARGET_ADDRESS_REGISTERS 0x2FA8
#define PCI_DID_BDW_EP_TARGET_ADDRESS_REGISTERS 0x6FA8

#define HSW_MMR_BASE_REG    0x48
#define HSW_REG_MAIN_CHAN0  0x5004
#define HSW_REG_MAIN_CHAN1  0x5008
#define HSW_REG_MCH_CFG     0x5E04
#define HSW_REG_TIMING_CAS  0x4014
#define HSW_REG_TIMING_RCD  0x4000


// EP register references and channel numbering: 330784-003, 331051-002, Linux arch/x86/events/intel/uncore_snbep.c .
// On several test platforms, timings are readable through ECAM on both HSW-EP and BDW-EP;
// still, the timing register is read opportunistically.
// Width comes from the TAD channel DIMMMTR registers and the type from the MCMTR, both standard
// config space; the frequency is measured independently through the PMON counter.
void get_imc_config_intel_hsw_ep(void)
{
    uint16_t id = imc.family == IMC_HSW_E ? PCI_DID_HSW_EP_TARGET_ADDRESS_REGISTERS : PCI_DID_BDW_EP_TARGET_ADDRESS_REGISTERS;
    unsigned int channels = 0;

    for (int bus = 0; bus < PCI_MAX_BUS && !channels; bus++) {
        if (pci_config_read32(bus, 19, 0, 0) != ((uint32_t)id << 16 | PCI_VID_INTEL)) continue;
        // Detect the two-controller arrangement through the HA1 channel TAD (2F6A/6F6A) at device 22 function 2.
        bool dual = pci_config_read32(bus, 22, 2, 0) == ((uint32_t)(id - 0x3E) << 16 | PCI_VID_INTEL);
        uint32_t timing = 0, config = 0;
        int first_dev = -1, first_func = 0;
        for (int mc = 0; mc < (dual ? 2 : 1); mc++) {
            int tad_dev = mc ? 22 : 19;
            uint16_t tad_id = (mc ? id - 0x40 : id) + 2;
            uint32_t mcmtr = pci_config_read32(bus, tad_dev, 0, 0x7C);
            for (int ch = 0; ch < (dual ? 2 : 4); ch++) {
                if (pci_config_read32(bus, tad_dev, ch + 2, 0) == ((uint32_t)(tad_id + ch) << 16 | PCI_VID_INTEL)) {
                    bool populated = false;
                    for (int slot = 0; slot < 3; slot++) {
                        uint32_t mtr = pci_config_read32(bus, tad_dev, ch + 2, 0x80 + 4 * slot);
                        if (mtr & (1 << 14)) populated = true;
                    }
                    if (populated) {
                        channels++;
                        if (first_dev < 0) {
                            config = mcmtr;
                            first_dev = (mc ? 23 : 20) + ch / 2;
                            first_func = ch % 2;
                            // Timings live in the extended configuration space of the
                            // channel thermal-control function.
                            uintptr_t addr = pci_config_mmio_addr(bus, first_dev, first_func);
                            if (addr && (*(volatile uint32_t *)addr & 0xFFFF) == PCI_VID_INTEL) {
                                timing = *(volatile uint32_t *)(addr + 0x200);
                            }
                        }
                    }
                }
            }
        }
        if (channels) {
            imc.type    = (config & (1 << 14)) ? "DDR4" : "DDR3";
            imc.width   = channels * 64;
            imc.tCL_dec = 0;
            imc.tCL     = (timing >> 9) & 0x1F;
            imc.tRCD    = timing & 0x1F;
            // tRP: DDR3 keeps the 4-bit field at [8:5]; DDR4 relocated it to 5 bits
            // at [31:27] (BDW-EP captures: DDR4-2400 CL17 -> 17 but [8:5] reads 0).
            // [18:14] decodes as tCWL, not tRP.
            imc.tRP     = (config & (1 << 14)) ? ((timing >> 27) & 0x1F) : ((timing >> 5) & 0xF);
            imc.tRAS    = (timing >> 19) & 0x3F;
            imc.freq    = get_imc_freq_intel_ep(bus, first_dev, first_func);
        }
    }
}

void get_imc_config_intel_hsw(void)
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
