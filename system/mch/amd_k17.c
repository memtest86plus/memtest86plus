// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for AMD K17 CPUs (Zen/Zen2)
//

#include "amd_smn.h"
#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "mch.h"

#define AMD_SMN_UMC_BAR             0x050000
#define AMD_SMN_UMC_CHB_OFFSET      0x100000
#define AMD_SMN_UMC_DRAM_CONFIG     AMD_SMN_UMC_BAR + 0x200
#define AMD_SMN_UMC_DRAM_TIMINGS1   AMD_SMN_UMC_BAR + 0x204
#define AMD_SMN_UMC_DRAM_TIMINGS2   AMD_SMN_UMC_BAR + 0x208

void get_imc_config_amd_k17(void)
{
    uint32_t smn_reg, offset;
    uint32_t reg_cha, reg_chb;

    imc.type    = "DDR4";
    imc.tCL_dec = 0;

    // Get Memory Mapped Register Base Address (Enable MMIO if needed)
    reg_cha = amd_smn_read(AMD_SMN_UMC_DRAM_CONFIG) & 0x7F;
    reg_chb = amd_smn_read(AMD_SMN_UMC_DRAM_CONFIG + AMD_SMN_UMC_CHB_OFFSET) & 0x7F;

    offset = reg_cha ? 0x0 : AMD_SMN_UMC_CHB_OFFSET;

    // Populate IMC width
    imc.width = (reg_cha && reg_chb) ? 128 : 64;

    // Get DRAM Frequency
    smn_reg = amd_smn_read(AMD_SMN_UMC_DRAM_CONFIG + offset) & 0x7F;

    switch(smn_reg) {
      case 0x14:
        imc.freq = 1333;
        break;
      case 0x18:
        imc.freq = 1600;
        break;
      case 0x1C:
        imc.freq = 1866;
        break;
      case 0x20:
        imc.freq = 2133;
        break;
      case 0x24:
        imc.freq = 2400;
        break;
      case 0x28:
        imc.freq = 2667;
        break;
      case 0x2C:
        imc.freq = 2933;
        break;
      case 0x30:
        imc.freq = 3200;
        break;
      default:
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
