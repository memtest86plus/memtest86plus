// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2025 Sam Demeulemeester
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

#define AMD_SMCA_BANK_STRIDE        0x10
#define AMD_SMCA_UMC_HWID           0x96
#define AMD_SMCA_MAX_UMC            16
#define AMD_UMC_VALID_ERROR_BIT     (1 << 31)
#define AMD_UMC_ERROR_CECC_BIT      (1 << 14)
#define AMD_UMC_ERROR_UECC_BIT      (1 << 13)
#define AMD_UMC_ERROR_CORE_ID_VAL   (1 << 24)
#define AMD_UMC_ERR_CNT_EN          (1 << 15)
#define AMD_MCA_STATUS_WR_ENABLE    (1 << 18)

#define ECC_RD_EN (1 << 10)
#define ECC_WR_EN (1 << 0)

// UMC MCA banks discovered via MCA_IPID (bank numbers differ between dies)
static struct {
    uint8_t bank;
    uint8_t umc;
} umc_banks[AMD_SMCA_MAX_UMC];
static uint8_t umc_bank_count = 0;

#if TESTWORD_WIDTH > 32
static void amd_smca_find_umc_banks(void)
{
    uint32_t regl, regh, nbanks;

    rdmsr(MSR_IA32_MCG_CAP, regl, regh);
    nbanks = regl & 0xFF;
    if (nbanks > 64) nbanks = 64;

    umc_bank_count = 0;
    for (uint32_t bank = 0; bank < nbanks && umc_bank_count < AMD_SMCA_MAX_UMC; bank++) {
        rdmsr(MSR_AMD64_SMCA_MC0_IPID + bank * AMD_SMCA_BANK_STRIDE, regl, regh);

        // IPID[43:32] HardwareID 0x96 with McaType[63:48] 0 (UMC) or 1 (UMC_V2); InstanceId[31:20] = UMC#
        if ((regh & 0xFFF) != AMD_SMCA_UMC_HWID || (regh >> 16) > 1) continue;

        umc_banks[umc_bank_count].bank = bank;
        umc_banks[umc_bank_count].umc  = (regl >> 20) & 0xFF;
        umc_bank_count++;
    }
}
#endif

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
            amd_smca_find_umc_banks();
        }

        if (umc_bank_count) {
            ecc_status.ecc_enabled = true;

            uint64_t mcg_ctl_bits = 0;
            for (uint8_t i = 0; i < umc_bank_count; i++) {
                mcg_ctl_bits |= 1ULL << umc_banks[i].bank;
            }

            // Enable ECC reporting
            rdmsr(MSR_IA32_MCG_CTL, regl, regh);
            wrmsr(MSR_IA32_MCG_CTL, regl | (uint32_t)mcg_ctl_bits, regh | (uint32_t)(mcg_ctl_bits >> 32));

            rdmsr(MSR_AMD64_HW_CONF, regl, regh);
            wrmsr(MSR_AMD64_HW_CONF, regl | AMD_MCA_STATUS_WR_ENABLE, regh); // Enable Write to MCA STATUS Register

            for (uint8_t i = 0; i < umc_bank_count; i++) {
                uint32_t ctl_msr = MSR_AMD64_SMCA_MC0_CTL + umc_banks[i].bank * AMD_SMCA_BANK_STRIDE;
                uint32_t cnt_sel = AMD_SMN_UMC_ECC_ERR_CNT_SEL + umc_banks[i].umc * AMD_SMN_UMC_CHB_OFFSET;

                rdmsr(ctl_msr, regl, regh);
                wrmsr(ctl_msr, regl | 1, regh);

                smn_reg = amd_smn_read(cnt_sel);
                amd_smn_write(cnt_sel, smn_reg | AMD_UMC_ERR_CNT_EN); // Enable UMC Error CNT
            }

            poll_ecc_amd_zen(false); // Clear ECC registers
        }
    }
#endif
}

void poll_ecc_amd_zen(bool report)
{
    uint32_t regh, regl;

    // Check all UMCs
    for (uint8_t i = 0; i < umc_bank_count; i++)
    {
        uint32_t status_msr = MSR_AMD64_SMCA_MC0_STATUS + umc_banks[i].bank * AMD_SMCA_BANK_STRIDE;
        uint32_t addr_msr   = MSR_AMD64_SMCA_MC0_ADDR   + umc_banks[i].bank * AMD_SMCA_BANK_STRIDE;
        uint8_t  umc        = umc_banks[i].umc;

        // Get Status Register
        rdmsr(status_msr, regl, regh);

        // Check if ECC error happened
        if (regh & AMD_UMC_VALID_ERROR_BIT) {

            // ErrorCodeExt 0 = DRAM ECC error (others are UMC-internal: clear without touching ECC count)
            if ((regl >> 16) & 0x3F) {
                wrmsr(status_msr, regl, regh & ~AMD_UMC_VALID_ERROR_BIT);
                continue;
            }

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
            ecc_status.core = (regh & AMD_UMC_ERROR_CORE_ID_VAL) ? regh & 0x3F : 0;

            // Get address
            rdmsr(addr_msr, regl, regh);

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
            rdmsr(status_msr, regl, regh);
            wrmsr(status_msr, regl, regh & ~AMD_UMC_VALID_ERROR_BIT);
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
