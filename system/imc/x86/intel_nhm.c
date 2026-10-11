// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Lionel Debroux
//
// ------------------------
//
// Platform-specific code for Intel Nehalem (NHM) and Westmere (WMR) CPUs

#include "cpuinfo.h"
#include "cpuid.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "imc.h"

// EP register references and channel numbering: 320835-003, 321322-002.
void get_imc_config_intel_nhm(void)
{
    // Exclude Clarkdale/Arrandale (0x25/0x26) and Nehalem-EX/Westmere-EX (0x2E/0x2F),
    // which have different IMCs.
    unsigned int model = cpuid_info.version.extendedModel << 4 | cpuid_info.version.model;
    uint16_t id;
    unsigned int max_channels;
    switch (model) {
      case 0x1A:                          // Bloomfield / Nehalem-EP
        id = 0x2C18;
        max_channels = 3;
        break;
      case 0x1E:                          // Lynnfield / Clarksfield
        id = 0x2C98;
        max_channels = 2;
        break;
      case 0x2C:                          // Gulftown / Westmere-EP
        id = 0x2D98;
        max_channels = 3;
        break;
      default:
        return;
    }

    for (int bus = 0; bus < PCI_MAX_BUS; bus++) {
        if (pci_config_read32(bus, 3, 0, 0) != ((uint32_t)id << 16 | PCI_VID_INTEL)) continue;
        // MC_CONTROL 0x48: bit 7 INIT_DONE is write-only (320835-003 2.8.1) and reads
        // back as 0, so it is not a usable "memory controller ready" gate. The
        // readable fields are CHANNEL[0..2]_ACTIVE at bits 8-10 and ECCEN at bit 1;
        // Linux i7core_edac.c uses the same bits.
        uint32_t control = pci_config_read32(bus, 3, 0, 0x48);
        uint32_t status  = pci_config_read32(bus, 3, 0, 0x4C);
        uint32_t max_dod = pci_config_read32(bus, 3, 0, 0x64);
        unsigned int slots = (max_dod & 3) + 1;    // MC_MAX_DOD[1:0]
        if (slots > 3) continue;

        unsigned int channels = 0;
        uint32_t first_bank = 0, first_mrs = 0;
        bool valid = true;
        for (unsigned int ch = 0; ch < max_channels; ch++) {
            // CHANNEL[ch]_ACTIVE (control bit 8+ch) and !CH_DISABLED (status bit ch).
            if (!(control & (1 << (8 + ch))) || (status & (1 << ch))) continue;
            int dev = 4 + ch;
            if (   pci_config_read32(bus, dev, 0, 0) != ((uint32_t)(id + 8 + ch * 8) << 16 | PCI_VID_INTEL)
                || pci_config_read32(bus, dev, 1, 0) != ((uint32_t)(id + 9 + ch * 8) << 16 | PCI_VID_INTEL)) {
                valid = false;
                break;
            }
            bool populated = false;
            for (unsigned int slot = 0; slot < slots; slot++) {
                uint32_t dod = pci_config_read32(bus, dev, 1, 0x48 + 4 * slot);
                if (dod & (1 << 9)) populated = true; // MC_DOD_CH: bit 9 set = DIMM present
            }
            if (!valid) break;
            if (!populated) continue;
            uint32_t bank = pci_config_read32(bus, dev, 0, 0x88);
            uint32_t mrs  = pci_config_read32(bus, dev, 0, 0x70);
            // MC_CHANNEL_x_MRS_VALUE_0_1 (0x70) holds the raw MR0 (15:0) and MR1
            // (31:16) values used for the training MRS writes (320835-003 2.10.7).
            // MR0 A6:A4 is the CAS Latency field: 001-111 -> CL 5-11 (JEDEC 79-3, MR0).
            // Hardware-validated: CL7 = 011 (W3550, DDR3-1066) and CL9 = 101 (Westmere-EP,
            // DDR3-1333), both with A2 = 0 -- A2 is not part of the CL encoding.
            // MC_CHANNEL_x_BANK_TIMING (0x88): tRP[3:0], tRAS[8:4], tRCD[12:9]
            // (320835-003 2.10.12).
            if (!(((mrs >> 4) & 7) >= 1)
                || !(bank & 0xF) || !((bank >> 9) & 0xF) || !((bank >> 4) & 0x1F)) {
                valid = false;
                break;
            }
            if (!channels) {
                first_bank = bank;
                first_mrs = mrs;
            }
            channels++;
        }
        if (valid && channels && pci_config_read32(bus, 3, 4, 0) == ((uint32_t)(id + 4) << 16 | PCI_VID_INTEL)) {
            // MC_DIMM_CLK_RATIO (dev 3 fn 4, 0x54): requested Qclk/Bclk ratio in [4:0];
            // 6 = DDR3-800, 8 = DDR3-1066, 10 = DDR3-1333 (320835-003 2.14.2). QCLK is the DIMM data rate.
            uint32_t clock = pci_config_read32(bus, 3, 4, 0x54);
            unsigned int dram_ratio = clock & 0x1F;
            if ((dram_ratio != 6 && dram_ratio != 8 && dram_ratio != 10) || !clks_per_msec) continue;
            uint32_t low, high;
            rdmsr(MSR_IA32_PLATFORM_INFO, low, high);
            unsigned int cpu_ratio = (low >> 8) & 0xFF;   // Maximum non-turbo ratio
            if (!cpu_ratio || cpu_ratio == 0xFF) continue;
            // QCLK is the DDR transfer rate, not DCLK: no extra factor of two.
            float freq = (float)clks_per_msec / 1000.0f / cpu_ratio * dram_ratio;
            if (freq >= 350.f && freq <= 3000.f) {
                // Publish only after all reads validate; use the first populated socket.
                imc.type    = "DDR3";
                imc.width   = channels * 64;
                imc.tCL_dec = 0;
                imc.tCL     = 4 + ((first_mrs >> 4) & 7);
                imc.tRCD    = (first_bank >> 9) & 0xF;
                imc.tRP     = first_bank & 0xF;
                imc.tRAS    = (first_bank >> 4) & 0x1F;
                imc.freq    = freq + 0.5f;
                break;
            }
        }
    }
}
