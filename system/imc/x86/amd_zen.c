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

// Data fabric (D18Fx), per-instance registers through the D18F4 indirect access
#define AMD_DF_DEV                  0x18
#define AMD_DF_FICAA_LEGACY         0x5C
#define AMD_DF_FICAD_LEGACY         0x98
#define AMD_DF_FICAA                0x8C
#define AMD_DF_FICAD                0xB8
#define AMD_DF_BROADCAST            0xFF
#define AMD_DF_TYPE_CS              4           // coherent station: one per memory channel

static bool df_legacy_access;

// As Linux drivers/ras/amd/atl/access.c: register of one fabric block instance, or of all (broadcast).
static uint32_t df_read(int instance, int func, int reg)
{
    uint32_t ficaa = (instance & 0xFF) << 16 | (func & 0x7) << 11 | (instance != AMD_DF_BROADCAST);

    ficaa |= df_legacy_access ? (reg & 0x7FC) : (reg & 0xFFC) >> 1;

    pci_config_write32(0, AMD_DF_DEV, 4, df_legacy_access ? AMD_DF_FICAA_LEGACY : AMD_DF_FICAA, ficaa);
    return pci_config_read32(0, AMD_DF_DEV, 4, df_legacy_access ? AMD_DF_FICAD_LEGACY : AMD_DF_FICAD);
}

static bool df_client_family(void)
{
    switch (imc.family) {
      case IMC_K17:
        // Summit / Pinnacle Ridge, Raven Ridge / Picasso, Renoir, Matisse. Not Rome / Castle Peak.
        return cpuid_info.version.extendedModel <= 1 || cpuid_info.version.extendedModel == 6
            || cpuid_info.version.extendedModel == 7;
      case IMC_K19_VRM:
      case IMC_K19_CZN:
      case IMC_K19_RPL:
      case IMC_K19_PHX:
      case IMC_K1A_GRG:
        return true;
      default:
        return false;
    }
}

// Register snapshot for the DIMM decoder (AM4 / AM5: one fabric node, two memory channels, four on Phoenix).
static void get_df_snapshot(void)
{
    uint32_t family = cpuid_info.version.family + cpuid_info.version.extendedFamily;
    uint32_t model  = cpuid_info.version.extendedModel << 4 | cpuid_info.version.model;

    if (!df_client_family() || pci_config_read16(0, AMD_DF_DEV, 0, 0x00) != 0x1022) {
        return;
    }
    if (pci_config_read16(0, AMD_DF_DEV + 1, 0, 0x00) == 0x1022) {
        amd_df.status = AMD_DF_BAD_TOPOLOGY;
        return;
    }
    amd_df.client = true;
    amd_df.ddr5   = (imc.family >= IMC_K19_RPL);        // DDR5 register layout (Linux amd64_edac zn_regs_v2)
    // AM4 processors without graphics have UMC0 on channel B (Summit / Pinnacle Ridge, Matisse, Vermeer). The AM4
    // APUs have it on channel A.
    amd_df.umc0_b = (imc.family == IMC_K19_VRM)
                 || (imc.family == IMC_K17 && (cpuid_info.version.extendedModel == 0
                                               || cpuid_info.version.extendedModel == 7));
    if (!enable_dimm_decode) {
        return;
    }

    // Linux check_for_legacy_df_access()
    df_legacy_access = family < 0x19 || (family == 0x19 && (model < 0x10 || (model >= 0x20 && model < 0x60)));

    // D18F0x40: the revision fields read as zero before DF4, where the fabric ID masks tell the revision.
    uint32_t reg = df_read(AMD_DF_BROADCAST, 0, 0x40);
    uint32_t major = (reg >> 24) & 0xF;
    uint32_t minor = (reg >> 16) & 0xFF;
    if (major == 0) {
        if (df_read(AMD_DF_BROADCAST, 1, 0x150) & 0xFFFF) {
            amd_df.rev = AMD_DF3P5;
        } else if (df_read(AMD_DF_BROADCAST, 1, 0x208) & 0x3FF) {
            amd_df.rev = AMD_DF3;
        } else {
            amd_df.rev = AMD_DF2;
        }
    } else if (major == 4 && minor <= 2) {
        amd_df.rev = (minor == 2) ? AMD_DF4P2 : AMD_DF4;
    } else {
        amd_df.status = AMD_DF_BAD_REVISION;
        return;
    }

    // The memory channels are the first fabric instances (= UMC number): two, and no more on a client part.
    // DF 4.2 (Phoenix) has four: one for each half of a DIMM.
    amd_df.split_ch = (amd_df.rev == AMD_DF4P2);
    amd_df.n_ch     = amd_df.split_ch ? 4 : 2;
    for (int i = 0; i <= amd_df.n_ch; i++) {
        if (((df_read(i, 0, 0x44) & 0xF) == AMD_DF_TYPE_CS) != (i < amd_df.n_ch)) {
            amd_df.status = AMD_DF_BAD_TOPOLOGY;
            return;
        }
    }

    bool df4 = (amd_df.rev >= AMD_DF4);
    int map_reg = (amd_df.rev == AMD_DF4P2) ? 0x200 : 0xE00;
    amd_df.hole_base = df_read(AMD_DF_BROADCAST, df4 ? 7 : 0, 0x104) & 0xFF000000;

    for (int ch = 0; ch < amd_df.n_ch; ch++) {
        amd_df.fabric_id[ch] = (df_read(ch, 0, 0x50) >> 8) & 0xFFF;

        for (int m = 0; m < (df4 ? AMD_DF_MAX_MAP : 2); m++) {
            amd_df_map_t *map = &amd_df.map[ch][m];
            if (df4) {
                map->base   = df_read(ch, 7, map_reg + 0x0 + 16 * m);
                map->limit  = df_read(ch, 7, map_reg + 0x4 + 16 * m);
                map->ctl    = df_read(ch, 7, map_reg + 0x8 + 16 * m);
                map->intlv  = df_read(ch, 7, map_reg + 0xC + 16 * m);
                map->offset = (m > 0) ? df_read(ch, 7, 0x140 + 4 * (m - 1)) : 0;
            } else {
                map->base   = df_read(ch, 0, 0x110 + 8 * m);
                map->limit  = df_read(ch, 0, 0x114 + 8 * m);
                map->ctl    = (amd_df.rev != AMD_DF2) ? df_read(ch, 0, 0x3F8) : 0;
                map->offset = (m > 0) ? df_read(ch, 0, 0x1B4 + 4 * (m - 1)) : 0;
            }
        }

        // UMC chip selects (offsets as Linux amd64_edac.h; chip select hash as illumos umc.h)
        uint32_t umc_base = AMD_SMN_UMC_BAR + ch * AMD_SMN_UMC_CHB_OFFSET;
        amd_umc_snap_t *umc = &amd_df.umc[ch];
        for (int cs = 0; cs < AMD_UMC_MAX_CS; cs++) {
            int mask_idx = amd_df.ddr5 ? cs : cs / 2;
            umc->base[cs]     = amd_smn_read(umc_base + 0x00 + 4 * cs);
            umc->base_sec[cs] = amd_smn_read(umc_base + 0x10 + 4 * cs);
            umc->mask[cs]     = amd_smn_read(umc_base + 0x20 + 4 * mask_idx);
            umc->mask_sec[cs] = amd_smn_read(umc_base + (amd_df.ddr5 ? 0x30 : 0x28) + 4 * mask_idx);
            umc->addr_cfg[cs] = amd_df.ddr5 ? amd_smn_read(umc_base + 0x40 + 4 * cs) : 0;
        }
        for (int i = 0; i < 2; i++) {
            umc->hash_cs[i] = amd_smn_read(umc_base + (amd_df.ddr5 ? 0xC8 : 0xE8) + 4 * i);
        }
    }

    amd_df.status = AMD_DF_OK;
    if (!amd_df_view(&amd_df, &imc_view)) {
        amd_df.status = AMD_DF_BAD_TOPOLOGY;
    }
}

void get_imc_config_amd_zen(void)
{
    uint32_t smn_reg, offset;
    uint32_t reg_cha, reg_chb;

    imc.tCL_dec = 0;

    get_df_snapshot();

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

    /* Select UMC MCA MSR base */
    uint32_t mca_ctrl_base = (imc.family >= IMC_K1A_GRG) ? MSR_AMD64_K1A_UMC_MCA_CTRL : MSR_AMD64_K17_UMC_MCA_CTRL;

    if (enable_ecc_polling) {
        uint32_t regl, regh;

        smn_reg = amd_smn_read(AMD_SMN_UMC_DRAM_ECC_CTRL + offset);
        if (smn_reg & (ECC_RD_EN | ECC_WR_EN)) {
            ecc_status.ecc_enabled = true;

            // Number of UMC to init
            uint8_t umc = 0, umc_max = 0;
            uint32_t umc_banks_bits = 0;

            if (imc.family == IMC_K19_VRM || imc.family == IMC_K19_RPL || imc.family == IMC_K19_RBT) {
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
            wrmsr(MSR_AMD64_HW_CONF, regl | AMD_MCA_STATUS_WR_ENABLE, regh); // Enable Write to MCA STATUS Register

            for (umc = 0; umc < umc_max; umc++)
            {
                rdmsr(mca_ctrl_base + (umc * AMD_UMC_OFFSET), regl, regh);
                wrmsr(mca_ctrl_base + (umc * AMD_UMC_OFFSET), regl | 1, regh);
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

    /* Select UMC MCA MSR base */
    uint32_t mca_status_base = (imc.family >= IMC_K1A_GRG) ? MSR_AMD64_K1A_UMC_MCA_STATUS : MSR_AMD64_K17_UMC_MCA_STATUS;
    uint32_t mca_addr_base   = (imc.family >= IMC_K1A_GRG) ? MSR_AMD64_K1A_UMC_MCA_ADDR   : MSR_AMD64_K17_UMC_MCA_ADDR;

    // Number of UMC to check
    if (imc.family == IMC_K19_VRM || imc.family == IMC_K19_RPL || imc.family == IMC_K19_RBT) {
        umc_max = 4;
    } else {
        umc_max = 2;
    }

    // Check all UMCs
    for (umc = 0; umc < umc_max; umc++)
    {
        // Get Status Register
        rdmsr(mca_status_base + (AMD_UMC_OFFSET * umc), regl, regh);

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
            rdmsr(mca_addr_base + (AMD_UMC_OFFSET * umc), regl, regh);

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
            rdmsr(mca_status_base + (AMD_UMC_OFFSET * umc), regl, regh);
            wrmsr(mca_status_base + (AMD_UMC_OFFSET * umc), regl, regh & ~AMD_UMC_VALID_ERROR_BIT);
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
