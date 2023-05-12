// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for AMD Zen CPUs
//

#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "imc.h"

#define AMD_SMN_UMC_BAR             0x050000
#define AMD_SMN_UMC_CHB_OFFSET      0x100000
#define AMD_SMN_UMC_DRAM_CONFIG     AMD_SMN_UMC_BAR + 0x200
#define AMD_SMN_UMC_DRAM_TIMINGS1   AMD_SMN_UMC_BAR + 0x204
#define AMD_SMN_UMC_DRAM_TIMINGS2   AMD_SMN_UMC_BAR + 0x208

void get_imc_config_amd_zen(void)
{
    uint32_t smn_reg, offset;
    uint32_t reg_cha, reg_chb;

    imc.tCL_dec = 0;

    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    reg_cha = amd_smn_read(AMD_SMN_UMC_DRAM_CONFIG) & 0x7F;
    reg_chb = amd_smn_read(AMD_SMN_UMC_DRAM_CONFIG + AMD_SMN_UMC_CHB_OFFSET) & 0x7F;

    offset = reg_cha ? 0x0 : AMD_SMN_UMC_CHB_OFFSET;

    // Populate IMC width
    imc.width = (reg_cha && reg_chb) ? 128 : 64;

    // Get DRAM Frequency
    smn_reg = amd_smn_read(AMD_SMN_UMC_DRAM_CONFIG + offset);
    if (imc.family >= IMC_K19_RPL) {
        imc.type = "DDR5";
        imc.freq = smn_reg & 0xFFFF;
        if ((smn_reg >> 18) & 1) imc.freq *= 2; // GearDown
    } else {
        imc.type = "DDR4";
        smn_reg = amd_smn_read(AMD_SMN_UMC_DRAM_CONFIG + offset) & 0x7F;
        imc.freq = (float)smn_reg * 66.67f;
    }

    if (imc.freq < 200 || imc.freq > 12000) {
        imc.freq = 0;
        return;
    }

    // Get Timings
    smn_reg = amd_smn_read(AMD_SMN_UMC_DRAM_TIMINGS1 + offset);

    // CAS Latency (tCAS)
    imc.tCL = smn_reg & 0x3F;

    // RAS Active to precharge (tRAS)
    imc.tRAS = (smn_reg >> 8) & 0x7F;

    // RAS-To-CAS (tRC)
    imc.tRCD = (smn_reg >> 16) & 0x3F;

    smn_reg = amd_smn_read(AMD_SMN_UMC_DRAM_TIMINGS2 + offset);

    // RAS Precharge (tRP)
    imc.tRP = (smn_reg >> 16) & 0x3F;
}
