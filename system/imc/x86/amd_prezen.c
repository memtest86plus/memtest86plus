// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Lionel Debroux
//
// ------------------------
// Platform-specific code for 64-bit, pre-Zen AMD CPUs:
// * AMD Family 0Fh CPUs (Athlon 64, Opteron);
// * AMD Family 10h CPUs (Phenom, Opteron 24xx/61xx);
// * AMD Family 11h CPUs (Turion X2 Ultra / Athlon X2 mobile, Griffin/Caspian);
// * AMD Family 12h models 00h-0Fh (Llano A-Series APUs) and Family 14h models 00h-0Fh (Bobcat C-/E-/Z-Series APUs).
// * AMD Family 15h models:
//     * Bulldozer 00h-0Fh CPUs (FX Zambezi, Opteron 32xx Zurich / 42xx Valencia / 62xx Interlagos);
//     * Piledriver 00h-0Fh CPUs (FX Vishera, Opteron 33xx Delhi / 43xx Seoul / 63xx Abu Dhabi);
//     * 10h-1Fh (Trinity, Richland), 30h-3Fh (Kaveri), 60h-6Fh (Carrizo), 70h-7Fh (Bristol Ridge, Stoney Ridge);
// * AMD Family 16h models 00h-0Fh (Kabini) and 30h-3Fh (Mullins).
//
// Register references: BKDG 26094/32559 (Family 0Fh), 31116 (10h), 41256 (11h), 41131 (12h), 43170 (14h),
// 42301/42300/49125/50742/55072 (15h), 48751/52740 (16h); Linux drivers/edac/amd64_edac.c.
//

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpuinfo.h"
#include "cpuid.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "imc.h"

#define AMD_NB_DEV          0x18    // node 0 northbridge
#define AMD_F3_NB_CAP       0xE8    // F3xE8[29] MultiNodeCpu
#define AMD_F1_DCT_SELECT   0x10C   // F1x10C DctCfgSel
#define AMD_F2_CS_BASE      0x40    // F2x[1,0][5C:40] DRAM CS Base, bit 0 CSEnable
#define AMD_F2_DRAM_CTRL    0x78    // F2x78[10:8] DramType (Family 15h models 60h-7Fh)
#define AMD_F2_TIMING_LOW   0x88
#define AMD_F2_DRAM_CFG     0x94    // F2x[1,0]94 DRAM Configuration High
#define AMD_F2_EXTRA_OFF    0xF0    // F2x[1,0]F0 DCT extra data offset (Family 12h/14h)
#define AMD_F2_EXTRA_PORT   0xF4
#define AMD_F2_TIMING_K15   0x200
#define AMD_DCT1_OFFSET     0x100

#define AMD_EXTRA_TIMING0   0x40    // DCT extra data "DRAM Timing 0": Trcd, Trp, Tras

// DCT register layout, i.e. how the frequency and the timings are decoded.
enum {
    AMD_K8_DDR,     // Family 0Fh revisions B-E
    AMD_K8_DDR2,    // Family 0Fh revisions F/G
    AMD_K10,        // Family 10h, DDR2 or DDR3
    AMD_K11,        // Family 11h, DDR2
    AMD_K12,        // Family 12h/14h, DDR3, row timings in the DCT extra data array
    AMD_K15,        // Family 15h/16h, DDR3, F2x200 timings
    AMD_K15_DDR4,   // Family 15h models 60h-7Fh, DDR3 or DDR4 from DramType, F2x200 timings
};

#define AMD_DUAL_NODE   (1 << 0)    // dual-die packages: node 1 is the second die
#define AMD_FREQ_CHG    (1 << 1)    // F2x94[21] FreqChgInProg exists
#define AMD_EXT_CFG     (1 << 2)    // registers above 0xFF need NB_CFG[46] EnableCf8ExtCfg

typedef struct __attribute__((packed)) {
    uint16_t family;        // IMC_K*
    uint8_t  model_min;     // CPUID extended model range
    uint8_t  model_max;
    uint16_t did;           // function 2 device ID
    uint16_t gang_reg;      // function 2 register holding the ganged-mode bit, 0 = none
    uint8_t  gang_bit;
    uint8_t  layout;
    uint8_t  num_dcts;
    uint8_t  sel_mask;      // F1x10C DctCfgSel mask; 0 = DCT1 registers at offset 0x100
    uint8_t  dct1_sel;      // DctCfgSel value selecting DCT1
    uint8_t  clk_valid_bit; // F2x94 MemClkFreqVal (MCR on K8 revisions B-E)
    uint8_t  flags;
} amd_dct_t;

static const amd_dct_t amd_dcts[] = {
    // family  models     DID   gang reg/bit  layout       DCTs sel dct1 clk  flags
    { IMC_K8,  0x0, 0x3, 0x1102, 0x090, 16, AMD_K8_DDR,   1, 0, 0, 25, 0                                          },
    { IMC_K8,  0x4, 0xF, 0x1102, 0x090, 11, AMD_K8_DDR2,  1, 0, 0,  3, 0                                          },
    { IMC_K10, 0x0, 0xF, 0x1202, 0x110,  4, AMD_K10,      2, 0, 0,  3, AMD_DUAL_NODE | AMD_FREQ_CHG | AMD_EXT_CFG },
    { IMC_K11, 0x0, 0xF, 0x1302, 0x110,  4, AMD_K11,      2, 0, 0,  3, AMD_EXT_CFG                                },
    { IMC_K12, 0x0, 0xF, 0x1702, 0x000,  0, AMD_K12,      2, 0, 0,  7, AMD_EXT_CFG                                },
    { IMC_K14, 0x0, 0xF, 0x1702, 0x000,  0, AMD_K12,      1, 0, 0,  7, 0                                          },
    { IMC_K15, 0x0, 0x0, 0x1602, 0x000,  0, AMD_K15,      2, 3, 1,  7, AMD_DUAL_NODE | AMD_FREQ_CHG | AMD_EXT_CFG },
    { IMC_K15, 0x1, 0x1, 0x1402, 0x000,  0, AMD_K15,      2, 3, 1,  7, AMD_FREQ_CHG | AMD_EXT_CFG                 },
    { IMC_K15, 0x3, 0x3, 0x141C, 0x000,  0, AMD_K15,      2, 3, 3,  7, AMD_FREQ_CHG | AMD_EXT_CFG                 },
    { IMC_K15, 0x6, 0x6, 0x1572, 0x000,  0, AMD_K15_DDR4, 2, 7, 1,  7, AMD_EXT_CFG                                },
    { IMC_K15, 0x7, 0x7, 0x15B2, 0x000,  0, AMD_K15_DDR4, 1, 0, 0,  7, AMD_EXT_CFG                                },
    { IMC_K16, 0x0, 0x0, 0x1532, 0x000,  0, AMD_K15,      1, 0, 0,  7, AMD_FREQ_CHG | AMD_EXT_CFG                 },
    { IMC_K16, 0x3, 0x3, 0x1582, 0x000,  0, AMD_K15,      1, 0, 0,  7, AMD_FREQ_CHG | AMD_EXT_CFG                 },
};

// -----------------------------------------------------------------------------

// Returns the data rate in MT/s from F2x94 (0 = reserved encoding) and sets imc.type.
static uint16_t decode_freq(uint8_t layout, uint32_t cfg, uint32_t dram_ctrl)
{
    static const uint16_t k8_ddr_rates[8]  = { 200, 0, 266, 0, 0, 333, 0, 400 };    // F2x94[22:20] MemClk
    static const uint16_t k8_ddr2_rates[8] = { 400, 533, 667, 800, 0, 0, 0, 0 };
    static const uint16_t k10_rates[2][8]  = {
        { 400, 533, 667, 800, 1066,    0,    0, 0 },   // DDR2
        {   0,   0, 667, 800, 1066, 1333, 1600, 0 },   // DDR3
    };
    static const uint16_t k11_rates[8]     = { 0, 533, 667, 800, 0, 0, 0, 0 };

    switch (layout) {
      case AMD_K8_DDR:
        imc.type = "DDR";
        return k8_ddr_rates[(cfg >> 20) & 7];
      case AMD_K8_DDR2:
        imc.type = "DDR2";
        return k8_ddr2_rates[cfg & 7];
      case AMD_K10: {
        bool ddr3 = cfg & (1 << 8);     // Ddr3Mode
        imc.type = ddr3 ? "DDR3" : "DDR2";
        return k10_rates[ddr3][cfg & 7];
      }
      case AMD_K11:
        imc.type = "DDR2";
        return k11_rates[cfg & 7];
      case AMD_K15_DDR4:
        switch ((dram_ctrl >> 8) & 7) {
          case 0:  imc.type = "DDR3"; break;
          case 2:  imc.type = "DDR4"; break;
          default: return 0;
        }
        break;
      default:
        imc.type = "DDR3";
        break;
    }

    // F2x[1,0]94[4:0] MemClkFreq, shared by all Family 12h-16h DCTs; reserved encodings are not filtered.
    switch (cfg & 0x1F) {
      case 0x02: return 400;    // Kaveri, Carrizo and Bristol Ridge only
      case 0x04: return 667;
      case 0x06: return 800;
      case 0x0A: return 1066;
      case 0x0E: return 1333;
      case 0x12: return 1600;
      case 0x16: return 1866;
      case 0x19: return 2100;   // Trinity only
      case 0x1A: return 2133;
      case 0x1F: return 2400;   // later Family 15h only
      default:   return 0;
    }
}

// Decodes the DRAM Timing Low register t (and the Family 12h/14h DRAM Timing 0 word) into imc.
static bool decode_timing(uint8_t layout, uint32_t cfg, uint32_t t, uint32_t timing0)
{
    imc.tCL_dec = 0;
    switch (layout) {
      case AMD_K8_DDR:
        // Tcl[2:0] 001b=2, 010b=3, 101b=2.5; Trcd[14:12], Trp[26:24], Tras[23:20] in clocks.
        switch (t & 7) {
          case 1: imc.tCL = 2; break;
          case 2: imc.tCL = 3; break;
          case 5: imc.tCL = 2; imc.tCL_dec = 5; break;
          default: return false;
        }
        imc.tRCD = (t >> 12) & 7;
        imc.tRP  = (t >> 24) & 7;
        imc.tRAS = (t >> 20) & 0xF;
        return imc.tRCD >= 2 && imc.tRCD <= 6 && imc.tRP >= 2 && imc.tRP <= 6 && imc.tRAS >= 5;

      case AMD_K8_DDR2:
        // Tcl[2:0]+1, Trcd[5:4]+3, Trp[9:8]+3, Tras[15:12]+3.
        imc.tCL  = (t & 7) + 1;
        imc.tRCD = ((t >> 4) & 3) + 3;
        imc.tRP  = ((t >> 8) & 3) + 3;
        imc.tRAS = ((t >> 12) & 0xF) + 3;
        return imc.tCL >= 3 && imc.tCL <= 6;

      case AMD_K10:
      case AMD_K11: {
        // BKDG 31116 F2x[1,0]88. Tcl[3:0]+4 on DDR3, +1 on DDR2.
        bool ddr3 = layout == AMD_K10 && (cfg & (1 << 8));
        imc.tCL = (t & 0xF) + (ddr3 ? 4 : 1);
        if (ddr3 || (layout == AMD_K10 && (cfg & 7) == 4)) {
            // DDR3 and DDR2-1066 (MemClkFreq 100b): Trcd[6:4]+5, Trp[9:7]+5, Tras[15:12]+15.
            imc.tRCD = ((t >> 4) & 7) + 5;
            imc.tRP  = ((t >> 7) & 7) + 5;
            imc.tRAS = ((t >> 12) & 0xF) + 15;
        } else {
            // DDR2: Trcd[5:4]+3, Trp[9:8]+3, Tras[15:12]+3.
            imc.tRCD = ((t >> 4) & 3) + 3;
            imc.tRP  = ((t >> 8) & 3) + 3;
            imc.tRAS = ((t >> 12) & 0xF) + 3;
        }
        return ddr3 ? imc.tCL <= 12 : (imc.tCL >= 3 && imc.tCL <= 7);
      }

      case AMD_K12:
        // Tcl[3:0]+4; DRAM Timing 0: Trcd[3:0]+5, Trp[11:8]+5, Tras[20:16]+15.
        imc.tCL  = (t & 0xF) + 4;
        imc.tRCD = (timing0 & 0xF) + 5;
        imc.tRP  = ((timing0 >> 8) & 0xF) + 5;
        imc.tRAS = ((timing0 >> 16) & 0x1F) + 15;
        return true;

      default:
        // F2x[1,0]200: Tcl[4:0], Trcd[12:8], Trp[20:16], Tras[29:24], already in clocks.
        imc.tCL  = t & 0x1F;
        imc.tRCD = (t >> 8) & 0x1F;
        imc.tRP  = (t >> 16) & 0x1F;
        imc.tRAS = (t >> 24) & 0x3F;
        return true;
    }
}

// Reads the selected DCT of node dev into imc. Returns the data rate, 0 if invalid.
static uint16_t read_dct(const amd_dct_t *row, int dev, int off, uint32_t cfg)
{
    if (!(cfg & (1 << row->clk_valid_bit)) || ((row->flags & AMD_FREQ_CHG) && (cfg & (1 << 21)))) {
        return 0;
    }

    uint32_t dram_ctrl = 0, timing0 = 0;
    if (row->layout == AMD_K15_DDR4) {
        dram_ctrl = pci_config_read32(0, dev, 2, AMD_F2_DRAM_CTRL + off);
    }
    if (row->layout == AMD_K12) {
        // Indirect read: write the register number, wait for DctAccessDone (bit 31), read the port.
        pci_config_write32(0, dev, 2, AMD_F2_EXTRA_OFF + off, AMD_EXTRA_TIMING0);
        int i = 0;
        while (!(pci_config_read32(0, dev, 2, AMD_F2_EXTRA_OFF + off) & (1U << 31))) {
            if (++i == 100) return 0;
        }
        timing0 = pci_config_read32(0, dev, 2, AMD_F2_EXTRA_PORT + off);
    }
    int timing_reg = row->layout >= AMD_K15 ? AMD_F2_TIMING_K15 : AMD_F2_TIMING_LOW;
    uint32_t t = pci_config_read32(0, dev, 2, timing_reg + off);

    uint16_t freq = decode_freq(row->layout, cfg, dram_ctrl);
    return (freq && decode_timing(row->layout, cfg, t, timing0)) ? freq : 0;
}

// -----------------------------------------------------------------------------

void get_imc_config_amd_prezen(void)
{
    const amd_dct_t *row = NULL;
    uint8_t model = cpuid_info.version.extendedModel;
    for (unsigned int i = 0; i < sizeof(amd_dcts) / sizeof(amd_dcts[0]); i++) {
        if (amd_dcts[i].family == imc.family && model >= amd_dcts[i].model_min && model <= amd_dcts[i].model_max) {
            row = &amd_dcts[i];
            break;
        }
    }
    if (!row) return;

    uint32_t nb_cfg_lo = 0, nb_cfg_hi = 0;
    if (row->flags & AMD_EXT_CFG) {
        rdmsr(MSR_AMD64_NB_CFG, nb_cfg_lo, nb_cfg_hi);
        wrmsr(MSR_AMD64_NB_CFG, nb_cfg_lo, nb_cfg_hi | (1 << 14));
    }

    // The width covers both dies of a dual-node package; the timings come from the first populated DCT.
    bool dual = (row->flags & AMD_DUAL_NODE) && (pci_config_read32(0, AMD_NB_DEV, 3, AMD_F3_NB_CAP) & (1 << 29));
    uint16_t freq = 0;
    int width = 0;
    bool valid = true;
    for (int dev = AMD_NB_DEV; valid && dev <= AMD_NB_DEV + dual; dev++) {
        if (pci_config_read32(0, dev, 2, 0) != ((uint32_t)row->did << 16 | PCI_VID_AMD)) break;
        bool ganged = row->gang_reg && (pci_config_read32(0, dev, 2, row->gang_reg) & (1 << row->gang_bit));
        uint32_t select = row->sel_mask ? pci_config_read32(0, dev, 1, AMD_F1_DCT_SELECT) : 0;

        for (int dct = 0; dct < (ganged ? 1 : row->num_dcts); dct++) {
            int off = 0;
            if (row->sel_mask) {
                uint32_t sel = (select & ~(uint32_t)row->sel_mask) | (dct ? row->dct1_sel : 0);
                pci_config_write32(0, dev, 1, AMD_F1_DCT_SELECT, sel);
                if (pci_config_read32(0, dev, 1, AMD_F1_DCT_SELECT) != sel) {
                    valid = false;
                    break;
                }
            } else {
                off = dct * AMD_DCT1_OFFSET;
            }

            uint32_t cfg = pci_config_read32(0, dev, 2, AMD_F2_DRAM_CFG + off);
            if (row->layout != AMD_K8_DDR && (cfg & (1 << 14))) continue;    // DisDramInterface
            bool populated = false;
            for (int cs = 0; cs < 8; cs++) {
                if (pci_config_read32(0, dev, 2, AMD_F2_CS_BASE + 4 * cs + off) & 1) populated = true;
            }
            if (!populated) continue;

            width += ganged ? 128 : 64;
            if (!freq) {
                freq = read_dct(row, dev, off, cfg);
                if (!freq) {
                    valid = false;
                    break;
                }
            }
        }
        if (row->sel_mask) {
            pci_config_write32(0, dev, 1, AMD_F1_DCT_SELECT, select);
        }
    }

    if (row->flags & AMD_EXT_CFG) {
        wrmsr(MSR_AMD64_NB_CFG, nb_cfg_lo, nb_cfg_hi);
    }

    if (valid && freq) {
        imc.width = width;
        imc.freq  = freq;
    }
}
