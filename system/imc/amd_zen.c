// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for AMD Zen CPUs
//

#include "error.h"

#include "config.h"
#include "cpuinfo.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "imc.h"

#include "display.h" // DEBUG

#define AMD_SMN_UMC_BAR             0x050000
#define AMD_SMN_UMC_CHB_OFFSET      0x100000
#define AMD_SMN_UMC_DRAM_ECC_CTRL   AMD_SMN_UMC_BAR + 0x14C
#define AMD_SMN_UMC_DRAM_CONFIG     AMD_SMN_UMC_BAR + 0x200
#define AMD_SMN_UMC_DRAM_TIMINGS1   AMD_SMN_UMC_BAR + 0x204
#define AMD_SMN_UMC_DRAM_TIMINGS2   AMD_SMN_UMC_BAR + 0x208

#define AMD_SMN_UMC_ECC_ERR_CNT_SEL AMD_SMN_UMC_BAR + 0xD80
#define AMD_SMN_UMC_ECC_ERR_CNT     AMD_SMN_UMC_BAR + 0xD84

#define AMD_UMC_OFFSET              0x10
#define AMD_UMC_VALID_ERROR_BIT     (1 << 31)
#define AMD_UMC_ERROR_CECC_BIT      (1 << 14)
#define AMD_UMC_ERROR_UECC_BIT      (1 << 13)
#define AMD_UMC_ERR_CNT_EN          (1 << 15)
#define AMD_MCG_CTL_2_BANKS         (1 << 16) | (1 << 15)
#define AMD_MCG_CTL_4_BANKS         (1 << 18) | (1 << 17) | (1 << 16) | (1 << 15)
#define AMD_MCA_STATUS_WR_ENABLE    (1 << 18)

#define ECC_RD_EN (1 << 10)
#define ECC_WR_EN (1 << 0)

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
    if (imc.family >= IMC_K19_RBT) {
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

    // Detect ECC (x64 only)
#if TESTWORD_WIDTH > 32
    if (enable_ecc_polling) {
        uint32_t regl, regh;

        smn_reg = amd_smn_read(AMD_SMN_UMC_DRAM_ECC_CTRL + offset);
        if (smn_reg & (ECC_RD_EN | ECC_WR_EN)) {
            ecc_status.ecc_enabled = true;

            // Number of UMC to init
            uint8_t umc = 0, umc_max = 0;
            uint32_t umc_banks_bits = 0;

            if (imc.family == IMC_K19_VRM || imc.family == IMC_K19_RPL) {
                umc_max = 4;
                umc_banks_bits = AMD_MCG_CTL_4_BANKS;
            } else {
                umc_max = 2;
                umc_banks_bits = AMD_MCG_CTL_2_BANKS;
            }

            // Enable ECC reporting
            rdmsr(MSR_IA32_MCG_CTL, regl, regh);
            wrmsr(MSR_IA32_MCG_CTL, regl | umc_banks_bits, regh);

            rdmsr(MSR_AMD64_HW_CONF, regl, regh);
            wrmsr(MSR_AMD64_HW_CONF, regl | AMD_MCA_STATUS_WR_ENABLE, regh); // // Enable Write to MCA STATUS Register

            for (umc = 0; umc < umc_max; umc++)
            {
                rdmsr(MSR_AMD64_UMC_MCA_CTRL + (umc * AMD_UMC_OFFSET), regl, regh);
                wrmsr(MSR_AMD64_UMC_MCA_CTRL + (umc * AMD_UMC_OFFSET), regl | 1, regh);
            }

            smn_reg = amd_smn_read(AMD_SMN_UMC_ECC_ERR_CNT_SEL);
            amd_smn_write(AMD_SMN_UMC_ECC_ERR_CNT_SEL, smn_reg | AMD_UMC_ERR_CNT_EN); // Enable CH0 Error CNT

            smn_reg = amd_smn_read(AMD_SMN_UMC_ECC_ERR_CNT_SEL + AMD_SMN_UMC_CHB_OFFSET);
            amd_smn_write(AMD_SMN_UMC_ECC_ERR_CNT_SEL + AMD_SMN_UMC_CHB_OFFSET, smn_reg | AMD_UMC_ERR_CNT_EN); // Enable CH1 Error CNT

            poll_ecc_amd_zen(false); // Clear ECC registers
        }
    }
#endif
}

void poll_ecc_amd_zen(bool report)
{
    uint8_t umc = 0, umc_max = 0;
    uint32_t regh, regl;

    // Number of UMC to check
    if (imc.family == IMC_K19_VRM || imc.family == IMC_K19_RPL) {
        umc_max = 4;
    } else {
        umc_max = 2;
    }

    // Check all UMCs
    for (umc = 0; umc < umc_max; umc++)
    {
        // Get Status Register
        rdmsr(MSR_AMD64_UMC_MCA_STATUS + (AMD_UMC_OFFSET * umc), regl, regh);

        // Check if ECC error happened
        if (regh & AMD_UMC_VALID_ERROR_BIT) {

            // Check the type or error. Currently, we only report Corrected ECC error
            // Uncorrected ECC errors are skipped to avoid double detection
            if (regh & AMD_UMC_ERROR_CECC_BIT) {
                ecc_status.type = ECC_ERR_CORRECTED;
            } else if (regh & AMD_UMC_ERROR_UECC_BIT) {
                ecc_status.type = ECC_ERR_UNCORRECTED;
            } else {
                ecc_status.type = ERR_UNKNOWN;
            }

            // Populate Channel Number
            ecc_status.channel = umc;

            // Get Core# associated with the error
            ecc_status.core = regh & 0x3F;

            // Get address
            rdmsr(MSR_AMD64_UMC_MCA_ADDR + (AMD_UMC_OFFSET * umc), regl, regh);

            ecc_status.addr = (uint64_t)(regh & 0x00FFFFFF) << 32;
            ecc_status.addr |= regl;

            // Clear Address n-th LSBs according to MSR bit[61:56]
            ecc_status.addr &= ~0ULL << ((regh >> 24) & 0x3F);

            // Get ECC Error Count
            ecc_status.count = amd_smn_read(AMD_SMN_UMC_ECC_ERR_CNT + (AMD_SMN_UMC_CHB_OFFSET * umc)) & 0xFFFF;
            if (!ecc_status.count) ecc_status.count++;

            // Report error
            if (report) {
                ecc_error();
            }

            // Clear Error
            rdmsr(MSR_AMD64_UMC_MCA_STATUS + (AMD_UMC_OFFSET * umc), regl, regh);
            wrmsr(MSR_AMD64_UMC_MCA_STATUS + (AMD_UMC_OFFSET * umc), regl, regh & ~AMD_UMC_VALID_ERROR_BIT);
            amd_smn_write(AMD_SMN_UMC_ECC_ERR_CNT + (AMD_SMN_UMC_CHB_OFFSET * umc), 0x0);

            // Clear Internal ECC Error status
            ecc_status.type     = ECC_ERR_NONE;
            ecc_status.addr     = 0;
            ecc_status.count    = 0;
            ecc_status.core     = 0;
            ecc_status.channel  = 0;
        }
    }
}
