// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Lionel Debroux
// Copyright (C) 2026 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Nehalem (NHM) and Westmere (WMR) CPUs
//
// References: Intel 320835, 321322; Linux i7core_edac.c.
//

#include <stdbool.h>
#include <stdint.h>

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "imc.h"

#define NHM_MC_DEV              3       // channel ch is device 4 + ch
#define NHM_MC_CONTROL          0x48    // [10:8] CHANNELx_ACTIVE
#define NHM_MC_STATUS           0x4C    // [2:0] CHANNELx_DISABLED
#define NHM_MC_CLK_RATIO_STATUS 0x50    // device 3 function 4, [4:0] current QCLK ratio: data rate / BCLK
#define NHM_CH_MRS_VALUE_0_1    0x70    // channel function 0, MR0 CAS latency in A6:A4 (low) and A2 (high)
#define NHM_CH_BANK_TIMING      0x88    // channel function 0, tRP[3:0], tRAS[8:4], tRCD[12:9]

void get_imc_config_intel_nhm(void)
{
    // Bloomfield / Nehalem-EP, Lynnfield / Clarksfield, Gulftown / Westmere-EP.
    static const uint16_t mc_dids[] = { 0x2C18, 0x2C98, 0x2D98 };

    // The uncore of socket 0 has the highest bus number.
    for (int bus = PCI_MAX_BUS - 1; bus >= 0; bus--) {
        uint32_t id = pci_config_read32(bus, NHM_MC_DEV, 0, 0);
        bool found = false;
        for (unsigned int i = 0; i < sizeof(mc_dids) / sizeof(mc_dids[0]); i++) {
            if (id == ((uint32_t)mc_dids[i] << 16 | PCI_VID_INTEL)) found = true;
        }
        if (!found) continue;

        uint32_t control = pci_config_read32(bus, NHM_MC_DEV, 0, NHM_MC_CONTROL);
        uint32_t status  = pci_config_read32(bus, NHM_MC_DEV, 0, NHM_MC_STATUS);
        unsigned int channels = 0;
        uint32_t bank = 0, mrs = 0;
        // Active channels are the populated ones; the DOD DIMM-present bits are not always set by the BIOS.
        for (int ch = 0; ch < 3; ch++) {
            if (!(control & (1 << (8 + ch))) || (status & (1 << ch))) continue;
            if (pci_config_read32(bus, 4 + ch, 0, 0) == 0xFFFFFFFF) continue;
            if (!channels++) {
                bank = pci_config_read32(bus, 4 + ch, 0, NHM_CH_BANK_TIMING);
                mrs  = pci_config_read32(bus, 4 + ch, 0, NHM_CH_MRS_VALUE_0_1);
            }
        }
        if (!channels) continue;

        // BCLK is the TSC rate divided by the maximum non-turbo ratio.
        uint32_t low, high;
        rdmsr(MSR_IA32_PLATFORM_INFO, low, high);
        unsigned int cpu_ratio = (low >> 8) & 0xFF;
        unsigned int dram_ratio = pci_config_read32(bus, NHM_MC_DEV, 4, NHM_MC_CLK_RATIO_STATUS) & 0x1F;
        // MR0 CL code is A2 (MSB) and A6:A4: codes 1-12 = CL 5-16.
        unsigned int cl_code = ((mrs >> 4) & 7) | ((mrs & 4) << 1);
        if (!cpu_ratio || !clks_per_msec || !cl_code) return;
        float freq = (float)clks_per_msec / 1000.0f / cpu_ratio * dram_ratio;
        if (freq < 350.0f || freq > 4000.0f) return;

        imc.type    = "DDR3";
        imc.width   = channels * 64;
        imc.tCL_dec = 0;
        imc.tCL     = 4 + cl_code;
        imc.tRCD    = (bank >> 9) & 0xF;
        imc.tRP     = bank & 0xF;
        imc.tRAS    = (bank >> 4) & 0x1F;
        imc.freq    = freq + 0.5f;
        return;
    }
}
