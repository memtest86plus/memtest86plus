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

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "cpuinfo.h"
#include "cpuid.h"
#include "memctrl.h"
#include "msr.h"
#include "pci.h"

#include "imc.h"

#define AMD_0FH_DRAM_TIMING        0x88
#define AMD_0FH_MODE               0x90

#define AMD_10H_DRAM_TIMING        0x88
#define AMD_10H_DCT_OFFSET         0x100
#define AMD_10H_DCT_GANG           0x110   // F2x110 is DCT Select Low on F10h

#define AMD_11H_DRAM_TIMING        0x88
#define AMD_11H_DCT_OFFSET         0x100
#define AMD_11H_DCT_GANG           0x110   // F2x110 is DRAM Controller Select Low on F11h

#define AMD_12H_14H_DRAM_TIMING    0x88
#define AMD_12H_14H_DCT_TIMING0    0x40
#define AMD_12H_14H_DCT_EXTRA_OFF  0xF0
#define AMD_12H_14H_DCT_EXTRA_PORT 0xF4
#define AMD_12H_14H_DCT_OFFSET     0x100

#define AMD_15H_DCT_SELECT         0x10C
#define AMD_15H_DRAM_TIMING        0x200


// -----------------------------------------------------------------------------
// Types for the Generic DCT reader shared by the pre-Zen AMD readers.

enum amd_family {
#if 0
    AMD_FAM_0F_PRENPT = 0,
    AMD_FAM_0F = 1,
#endif
    AMD_FAM_10 = 2,
#if 0
    AMD_FAM_11 = 3,
#endif
#if 0
    AMD_FAM_12 = 4,
    AMD_FAM_14 = 5,
#endif
    AMD_FAM_15_00_0F = 6,
    AMD_FAM_15_10_1F = 7,
    AMD_FAM_15_30_3F = 8,
    AMD_FAM_15_60_6F = 9,
    AMD_FAM_15_70_7F = 10,
    AMD_FAM_16 = 11,
};

enum amd_dct_select_mode {
    AMD_DCT_DIRECT,   // single DCT at the base register offsets
    AMD_DCT_OFFSET,   // DCT1 registers at base + dct_offset (Family 10h)
    AMD_DCT_CFG_SEL,  // DCT selected through DctCfgSel (Family 15h)
};

struct __attribute__((packed)) amd_dct_caps {
    uint16_t dev_did;        // function 2 PCI device ID (vendor PCI_VID_AMD)
    uint8_t  num_dcts;       // DRAM controllers per node
    bool     needs_ext_cfg;  // enable extended config cycles (NB_CFG[46]) for offsets >= 0x100
    uint8_t  select_mode;    // enum amd_dct_select_mode
    uint16_t select_reg;     // CFG_SEL: DctCfgSel register (function 1)
    uint8_t  select_mask;    // CFG_SEL: DctCfgSel field mask
    uint8_t  dct1_select;    // CFG_SEL: DctCfgSel value selecting DCT1
    uint16_t dct_offset;     // OFFSET: DCT1 register offset
    bool     has_dual_node;  // dual-die packages (F3xE8)
    uint8_t  dual_node_bit;  // MultiNodeCpu / DualNode bit
    bool     has_gang;       // ganged mode: one 128-bit channel, DCT1 is not separate
    uint16_t gang_reg;       // function 2 register holding the gang bit
    uint8_t  gang_bit;
    uint8_t  freq_valid_bit; // F2x94 memory clock valid bit (MemClkFreqVal / MCR)
    bool     has_freq_chg;   // F2x94 FreqChgInProg bit exists
    uint8_t  freq_chg_bit;
    bool     has_dis_iface;  // F2x94[14] DisDramInterface bit exists
    bool     type_by_ddr3;   // memory type depends on the DDR3 mode bit
    uint16_t timing_reg;     // function 2 DRAM Timing Low register
    uint16_t row_timing_idx; // Family 12h/14h: DRAM Timing 0 (Trcd, Trp, Tras) index in the DCT extra data array; 0 = row timings in timing_reg
    uint16_t dram_type_reg;  // function 2 register holding DramType[10:8] (0 = none)
    const char type[5];      // memory type, or empty to pick it from the DCT registers
    const char type_alt[5];  // memory type when not DDR3, or empty for DDR2
};



// -----------------------------------------------------------------------------

static inline __attribute__((always_inline)) uint16_t decode_freq(enum amd_family family, uint32_t config, uint32_t dram_ctrl, bool *is_ddr3)
{
#if 0
    if (family == AMD_FAM_0F_PRENPT) {
        // F2x94[22:20] MemClk: 000b=100, 010b=133, 101b=166, 111b=200 MHz; the
        // rate is twice the MEMCLK frequency. Validity is gated by bit 25 MCR
        // (set by the BIOS once the memory clock ratio is programmed); there is
        // no MemClkFreqVal before rev F.
        static const uint16_t amd_0fh_prenpt_rates[8] = { 200, 0, 266, 0, 0, 333, 0, 400 };

        *is_ddr3 = false;
        return amd_0fh_prenpt_rates[((config >> 20) & 0x7)];
    }
    else if (family == AMD_FAM_0F) {
        // F2x94[2:0] MemClkFreq: 000b-011b = 200/266/333/400 MHz MEMCLK, 1xx
        // reserved; the rate is twice the MEMCLK frequency.
        static const uint16_t amd_0fh_rates[8] = { 400, 533, 667, 800, 0, 0, 0, 0 };

        *is_ddr3 = false;
        return amd_0fh_rates[config & 0x7];
    }
    else
#endif
    if (family == AMD_FAM_10) {
        // F2x[1,0]94 MemClkFreq [2:0] rates; the encoding depends on F2x94[8]
        // Ddr3Mode. The rate is twice the MEMCLK frequency.
        static const uint16_t amd_10h_rates[2][8] = {
            { 400,  533,  667,  800, 1066,    0,    0, /* reserved */ 0 }, // DDR2 rates
            {   0,    0,  667,  800, 1066, 1333, 1600, /* reserved */ 0 }  // DDR3 rates
        };

        *is_ddr3 = (config & (1U << 8)) != 0;   // 8: Ddr3Mode
        return amd_10h_rates[*is_ddr3][config & 0x7];
    }
#if 0
    else if (family == AMD_FAM_11) {
        // Family 11h (Griffin/Caspian mobile, BKDG 41256 Rev 3.00): DDR2
        // only (no Ddr3Mode in F2x94). F2x94[2:0] MemClkFreq is a 3-bit
        // K8-style field, unlike the 5-bit field of the DDR3-era DCTs:
        // 001b = 266 MHz = DDR2-533, 010b = 333 MHz = DDR2-667,
        // 011b = 400 MHz = DDR2-800, the other encodings are reserved.
        // Validity is gated by F2x94[3] MemClkFreqVal, like Family 10h.
        static const uint16_t amd_11h_rates[8] = { 0, 533, 667, 800, 0, 0, 0, 0 };

        *is_ddr3 = false;
        return amd_11h_rates[config & 0x7];
    }
#endif
    else if (
#if 0
        family == AMD_FAM_12 || family == AMD_FAM_14 ||
#endif
        family == AMD_FAM_15_00_0F || family == AMD_FAM_15_10_1F || family == AMD_FAM_15_30_3F || family == AMD_FAM_16) {
        *is_ddr3 = true;   // the other Family 12h/14h/15h/16h DCTs are DDR3 only
    }
    else if (family == AMD_FAM_15_60_6F || family == AMD_FAM_15_70_7F) {
        // The DCT speaks DDR3 or DDR4, selected by F2x78[10:8] DramType:
        // 000b = DDR3, 010b = DDR4; the other values are reserved.
        uint32_t dram_type = (dram_ctrl >> 8) & 0x7;
        if (dram_type != 0x0 && dram_type != 0x2) return 0;
        *is_ddr3 = (dram_type == 0x0);
    }
    else {
        return 0; // Shouldn't happen anyway.
    }

    // F2x[1,0]94[4:0] MemClkFreq rates; the rate is twice the MEMCLK
    // frequency. All Family 12h, 14h, 15h and 16h DCTs share the encoding
    // (BKDG 42301 Table 165, 42300 Table 132, 49125 Table 141, 48751
    // Table 125, 52740 Table 137, 50742 Table 124, 55072 Table 118,
    // 41131 p.264, 43170 p.242; not every rate is valid on every family,
    // the reserved encodings are not filtered).
    switch (config & 0x1F) {
        case 0x02: return 400;  // Documented on Kaveri, Carrizo and Bristol Ridge only
        case 0x04: return 667;
        case 0x06: return 800;
        case 0x0A: return 1066;
        case 0x0E: return 1333;
        case 0x12: return 1600;
        case 0x16: return 1866;
        case 0x19: return 2100; // Documented only on Trinity
        case 0x1A: return 2133;
        case 0x1F: return 2400; // Documented only on later Fam15h.
        default:   return 0;
    }
}

static inline __attribute__((always_inline)) bool decode_timing(enum amd_family family, uint32_t t, uint32_t row_timing, bool ddr3, uint8_t *tCL, uint8_t *tCL_dec, uint8_t *tRCD, uint8_t *tRP, uint8_t *tRAS)
{
#if 0
    if (family == AMD_FAM_0F_PRENPT) {
        // Revisions B-E (Post package): DDR1.
        // Register references: BKDG 26094 Rev 3.30: DDR1: F2x88 p.102-104, F2x90 p.106-107, F2x94 p.112-115.
        //
        // F2x88 DRAM Timing Low: Tcl[2:0] 001b=2, 010b=3, 101b=2.5 (others
        // reserved; the half cycle is reported through tCL_dec), Trcd[14:12]
        // 010b=2 .. 110b=6, Trp[26:24] 010b=2 .. 110b=6, Tras[23:20] 0101b=5 ..
        // 1111b=15.
        (void)ddr3, (void)row_timing;
        switch (t & 0x7) {
            case 1: *tCL = 2; *tCL_dec = 0; break;
            case 2: *tCL = 3; *tCL_dec = 0; break;
            case 5: *tCL = 2; *tCL_dec = 5; break;
            default: return false;
        }
        *tRCD = (t >> 12) & 0x7;
        *tRP  = (t >> 24) & 0x7;
        *tRAS = (t >> 20) & 0xF;
        return *tRCD >= 2 && *tRCD <= 6 && *tRP >= 2 && *tRP <= 6 && *tRAS >= 5 && *tRAS <= 15;
    }
    else if (family == AMD_FAM_0F) {
        // Revisions F/G (Non-Post Package), DDR2.
        // Register references: BKDG 32559 F/G: DDR2: F2x88 p.104-105, F2x90 p.109, F2x94 p.111-112.
        //
        // F2x88 DRAM Timing Low: Tcl[2:0] 010b=3 .. 101b=6 (others reserved),
        // Trcd[5:4] 00b=3 .. 11b=6, Trp[9:8] 00b=3 .. 11b=6, Tras[15:12] 2h=5 ..
        // Fh=18.
        (void)ddr3, (void)row_timing;
        *tCL_dec = 0;
        *tCL     = ((t >> 0) & 0x7) + 1;
        if (*tCL < 3 || *tCL > 6) return false;
        *tRCD    = ((t >> 4) & 0x3) + 3;
        *tRP     = ((t >> 8) & 0x3) + 3;
        *tRAS    = ((t >> 12) & 0xF) + 3;
        return true;
    }
    else
#endif
    if (family == AMD_FAM_10) {
        // Register references: BKDG 31116 Rev 3.62 (F2x[1,0]88 p.244-246, F2x[1,0]94
        // p.252-256, F2x110 p.257+, F2x[5C:40], F1x60, F3xE8; Linux amd64_edac.c).
        //
        // F2x[1,0]x DCT0/DCT1: DCT1 registers live at offset+0x100 (no DctCfgSel).
        // F2x[1,0]88 DRAM Timing Low (BKDG p.244-246). Field encodings differ
        // between DDR2 and DDR3 (F2x94[8] Ddr3Mode); every extraction below is
        // branch-per-type. Returns false for reserved encodings.
        (void)row_timing;
        *tCL_dec = 0;
        if (!ddr3) {
            // Tcl: 0010b=3 .. 0110b=7; Trcd [6:4] x00b=3 .. x11b=6;
            // Trp [8:7] 00xb=3 .. 11xb=6; Tras [15:12] 2h=5 .. Fh=18.
            *tCL  = ((t >> 0) & 0x7) + 1;
            if (*tCL < 3 || *tCL > 7) return false;
            *tRCD = ((t >> 4) & 0x3) + 3;
            *tRP  = ((t >> 7) & 0x3) + 3;
            *tRAS = ((t >> 12) & 0xF) + 3;
        } else {
            // Tcl: 0000b=4 .. 1000b=12; Trcd [6:4] 000b=5 .. 111b=12;
            // Trp [9:7] 000b=5 .. 111b=12; Tras [15:12] 0h=15 .. Fh=30.
            *tCL  = ((t >> 0) & 0xF) + 4;
            if (*tCL < 4 || *tCL > 12) return false;
            *tRCD = ((t >> 4) & 0x7) + 5;
            *tRP  = ((t >> 7) & 0x7) + 5;
            *tRAS = ((t >> 12) & 0xF) + 15;
        }
        return true;
    }
#if 0
    else if (family == AMD_FAM_11) {
        // Family 11h (Griffin/Caspian mobile, BKDG 41256 Rev 3.00,
        // F2x[1,0]88): DDR2 only. Tcl [3:0] 0010b=3 .. 0101b=6
        // (0000b/0001b/0110b-1111b reserved, i.e. value + 1),
        // Trcd [6:4] x00b=3 .. x11b=6, Trp [9:7] 00xb=3 .. 11xb=6,
        // Tras [15:12] 2h=5 .. Fh=18. A 4-bit Tcl and 3-bit Trcd/Trp
        // fields: unlike both the K8 revision F and the Family 10h
        // layouts.
        (void)ddr3;
        (void)row_timing;
        *tCL_dec = 0;
        *tCL     = ((t >> 0) & 0xF) + 1;
        if (*tCL < 3 || *tCL > 6) return false;
        *tRCD    = ((t >> 4) & 0x7) + 3;
        *tRP     = ((t >> 7) & 0x7) + 3;
        *tRAS    = ((t >> 12) & 0xF) + 3;
        return true;
    }
#endif
#if 0
    else if (family == AMD_FAM_12 || family == AMD_FAM_14) {
        // Family 12h (Llano, BKDG 41131 Rev 3.02) and Family 14h (Bobcat,
        // BKDG 43170 Rev 3.13). Only Tcl lives in config space, in
        // F2x[1,0]88[3:0] (value + 4, i.e. 1h-Ah = 5-14 clocks; 41131
        // p.259, 43170 p.234). The row timings are not in config space:
        // they live in the per-DCT extended data array, register x40
        // "DRAM Timing 0" (Trcd[3:0]+5, Trp[11:8]+5, Tras[20:16]+15,
        // Trc[29:24]+16), reached through the F2x[1,0]F0 indirect access
        // port followed by a read of F2x[1,0]F4 (41131 p.291/p.296,
        // 43170 p.296; both BKDGs document the same encodings). The row
        // timing word is passed in row_timing by the reader.
        (void)ddr3;
        *tCL_dec = 0;
        *tCL     = ((t >> 0) & 0xF) + 4;
        *tRCD    = (row_timing & 0xF) + 5;
        *tRP     = ((row_timing >> 8) & 0xF) + 5;
        *tRAS    = ((row_timing >> 16) & 0x1F) + 15;
        return true;
    }
#endif
    else if (family == AMD_FAM_15_00_0F || family == AMD_FAM_15_10_1F || family == AMD_FAM_15_30_3F || family == AMD_FAM_15_60_6F || family == AMD_FAM_15_70_7F || family == AMD_FAM_16) {
        // F2x[1,0]200 DRAM Timing Low: Tcl[4:0], Trcd[12:8], Trp[20:16],
        // Tras[29:24] (BKDG 42301 p.510+, 42300 p.398+, 49125 p.419,
        // 48751 p.369, 50742, 55072: the same layout on all Family 15h/16h DCTs,
        // DDR3 and DDR4 alike); the fields are already cycle counts.
        (void)ddr3, (void)row_timing;
        *tCL_dec = 0;
        *tCL     = t & 0x1F;
        *tRCD    = (t >> 8) & 0x1F;
        *tRP     = (t >> 16) & 0x1F;
        *tRAS    = (t >> 24) & 0x3F;
        return true;
    }
    return false; // Shouldn't happen anyway.
}



// -----------------------------------------------------------------------------
// Generic DCT reader shared by the pre-Zen AMD readers (Family 0Fh, 10h,
// 15h, 16h). Scans the northbridge function 2 devices, walks the DCTs of each
// node and publishes the first consistent DRAM configuration found.
// All per-family register encodings live in the amd_dct_caps tables.
//
// Register references: BKDG 26094/32559 (Family 0Fh), 31116 (Family 10h),
// 42301/42300/49125/50742/55072 (Family 15h), 48751/52740 (Family 16h); Linux
// drivers/edac/amd64_edac.c follows the same node and DCT enumeration.
//
// Family 10h register-map differences from Family 0Fh:
// * northbridge function 2 is DID 1202, not 1102;
// * node DRAM Base/Limit 0-7 live at F1x[7C:40], 16-MB granular in
//   reg[31:16] (SysAddr[39:24]) with the top bits in F1x[140/0x144],
//   while F1x[BC:80] holds the MMIO Base/Limit windows (the opposite of
//   the K8 register map, where F1x[BC:80] held DRAM Base/Limit);
// * DRAM CS Base registers at F2x[1,0][5C:40] still carry CSEnable at
//   bit 0 (the only field this reader uses), but they pair with
//   per-CS-pair CS Mask registers at F2x[1,0][6C:60], and their address
//   fields are split across reg bits [13:5] and [28:19] (SysAddr [21:13]
//   and [36:27]) rather than K8's contiguous [31:21];
// * F2x78 is DRAM Control, not DCT Select: the DCT Select Low/High
//   registers are F2x110/0x114, and only Family 10h has DctGangEn
//   (F2x110[4]).

#define AMD_DCT_DRAM_CFG  0x94
#define AMD_DCT_CS_BASE   0x40

static void get_imc_config_amd_nb_dct(enum amd_family family, const struct amd_dct_caps *caps)
{
    uint32_t msr_low = 0, msr_high = 0;

    // Registers at offsets >= 0x100 (DCT1 offsets, F2x110, F2x200) need
    // NB_CFG[46] EnableCf8ExtCfg (high dword, bit 14); save NB_CFG and
    // restore it before returning, including on early bail-outs.
    if (caps->needs_ext_cfg) {
        rdmsr(MSR_AMD64_NB_CFG, msr_low, msr_high);
        wrmsr(MSR_AMD64_NB_CFG, msr_low, msr_high | (1 << 14));
    }

    for (int first = 0x18; first < 0x20; first++) {
        if (pci_config_read32(0, first, 2, 0) != (((uint32_t)caps->dev_did << 16) | PCI_VID_AMD)) continue;
        uint32_t cap = pci_config_read32(0, first, 3, 0xE8);
        uint32_t node = pci_config_read32(0, first, 0, 0x60);
        // Dual-die packages (G34) expose two northbridges; the peer die
        // is the node with the matching even/odd NodeId pair.
        bool dual = caps->has_dual_node && (cap & (1U << caps->dual_node_bit)) != 0;
        uint32_t timing = 0;
        uint32_t row_timing = 0;
        uint16_t freq = 0;
        bool ddr3 = false;
        int channels = 0;
        int width = 0;
        bool valid = true;

        for (int dev = 0x18; dev < 0x20; dev++) {
            if (!dual && dev != first) continue;
            if (pci_config_read32(0, dev, 2, 0) != (((uint32_t)caps->dev_did << 16) | PCI_VID_AMD)) continue;
            if (dual) {
                uint32_t peer_cap = pci_config_read32(0, dev, 3, 0xE8);
                uint32_t peer_node = pci_config_read32(0, dev, 0, 0x60);
                if (!(peer_cap & (1U << caps->dual_node_bit)) || (peer_node & 6) != (node & 6)) continue;
            }
            // Ganged mode: the interface is one 128-bit channel driven by
            // DCT0; DCT1 is not a separate channel (Linux amd64_edac).
            bool ganged = caps->has_gang && (pci_config_read32(0, dev, 2, caps->gang_reg) & (1U << caps->gang_bit)) != 0;
            // CFG_SEL: DctCfgSel also holds DctCfgBcEn, NbPsSel and
            // MemPsSel, so save it, read-modify-write only the DctCfgSel
            // field and restore it afterwards, including on invalid reads.
            uint32_t select = 0;
            if (caps->select_mode == AMD_DCT_CFG_SEL) {
                select = pci_config_read32(0, dev, 1, caps->select_reg);
            }
            for (int dct = 0; dct < caps->num_dcts; dct++) {
                if (dct && ganged) break;
                uint32_t off = 0;
                if (caps->select_mode == AMD_DCT_OFFSET) {
                    off = dct * caps->dct_offset;
                } else if (caps->select_mode == AMD_DCT_CFG_SEL) {
                    uint32_t sel = (select & ~(uint32_t)caps->select_mask) | (dct ? caps->dct1_select : 0);
                    pci_config_write32(0, dev, 1, caps->select_reg, sel);
                    if (pci_config_read32(0, dev, 1, caps->select_reg) != sel) {
                        valid = false;
                        break;
                    }
                }
                uint32_t config = pci_config_read32(0, dev, 2, AMD_DCT_DRAM_CFG + off);
                uint32_t dram_ctrl = caps->dram_type_reg ? pci_config_read32(0, dev, 2, caps->dram_type_reg + off) : 0;
                if (caps->has_dis_iface && (config & (1U << 14))) continue;   // 14: DisDramInterface
                bool populated = false;
                for (int cs = 0; cs < 8; cs++) {
                    uint32_t base = pci_config_read32(0, dev, 2, AMD_DCT_CS_BASE + 4 * cs + off);
                    if (base & 1) populated = true;  // CSEnable
                }
                if (!populated) continue;
                if (!(config & (1U << caps->freq_valid_bit))
                    || (caps->has_freq_chg && (config & (1U << caps->freq_chg_bit)))) {
                    valid = false;   // memory clock not valid or frequency change in progress
                    break;
                }
                bool is_ddr3 = false;
                uint16_t rate = decode_freq(family, config, dram_ctrl, &is_ddr3);
                uint32_t reg = pci_config_read32(0, dev, 2, caps->timing_reg + off);
                // Family 12h/14h: the row timings live in the DCT extra data
                // array, behind the F2x[1,0]F0 indirect access port: write the
                // register number (DctAccessWrite=0 for a read), then read the
                // data from F2x[1,0]F4 (BKDG 41131 p.291, 43170 p.296).
                uint32_t row_tmg = 0;
#if 0
                if (caps->row_timing_idx) {
                    pci_config_write32(0, dev, 2, AMD_12H_14H_DCT_EXTRA_OFF + off, caps->row_timing_idx);
                    bool done = false;
                    for (int i = 0; i < 100 && !done; i++) {
                        done = (pci_config_read32(0, dev, 2, AMD_12H_14H_DCT_EXTRA_OFF + off) & (1U << 31)) != 0;  // 31: DctAccessDone
                    }
                    if (!done) {
                        valid = false;   // DCT extra data array not answering
                        break;
                    }
                    row_tmg = pci_config_read32(0, dev, 2, AMD_12H_14H_DCT_EXTRA_PORT + off);
                }
#endif
                uint8_t tCL, tCL_dec, tRCD, tRP, tRAS;
                if (   !rate || (freq && (freq != rate || (caps->type_by_ddr3 && ddr3 != is_ddr3)))
                    || !decode_timing(family, reg, row_tmg, is_ddr3, &tCL, &tCL_dec, &tRCD, &tRP, &tRAS)) {
                    valid = false;
                    break;
                }
                if (!channels) {
                    timing = reg;
                    row_timing = row_tmg;
                    freq = rate;
                    ddr3 = is_ddr3;
                }
                channels++;
                width += ganged ? 128 : 64;
            }
            if (caps->select_mode == AMD_DCT_CFG_SEL) {
                pci_config_write32(0, dev, 1, caps->select_reg, select);
            }
            if (!valid) break;
        }
        if (valid && channels) {
            uint8_t tCL = 0, tCL_dec = 0, tRCD = 0, tRP = 0, tRAS = 0;
            imc.type    = caps->type[0] ? caps->type : (ddr3 ? "DDR3" : (caps->type_alt[0] ? caps->type_alt : "DDR2"));
            imc.width   = width;
            imc.freq    = freq;
            (void)decode_timing(family, timing, row_timing, ddr3, &tCL, &tCL_dec, &tRCD, &tRP, &tRAS);
            imc.tCL_dec = tCL_dec;
            imc.tCL     = tCL;
            imc.tRCD    = tRCD;
            imc.tRP     = tRP;
            imc.tRAS    = tRAS;
            break;
        }
    }

    if (caps->needs_ext_cfg) {
        wrmsr(MSR_AMD64_NB_CFG, msr_low, msr_high);
    }
}



// -----------------------------------------------------------------------------
// High-level entry point.
void get_imc_config_amd_prezen(uint16_t imc_family)
{
#if 0
    static const struct amd_dct_caps amd_0fh_caps[2] = {
        { // Revisions B-E (DDR1, sockets 940/939/754). F2x90[10] DramEnable
          // only exists from rev C onwards, so the engine must not gate on
          // it. The 128/64 bit mode bit is F2x90[16] (bit 11 is the read-only
          // MemClrStatus there).
            .dev_did        = 0x1102,
            .num_dcts       = 1,
            .needs_ext_cfg  = false,
            .select_mode    = AMD_DCT_DIRECT,
            .has_dual_node  = false,
            .has_gang       = true,
            .gang_reg       = AMD_0FH_MODE,
            .gang_bit       = 16,               // F2x90[16] 128/64 bit mode
            .freq_valid_bit = 25,               // F2x94[25] MCR
            .has_freq_chg   = false,
            .has_dis_iface  = false,            // no DisDramInterface before rev F
            .type_by_ddr3   = false,
            .timing_reg     = AMD_0FH_DRAM_TIMING,
            .type           = "DDR",
        },
        { // Revisions F/G (Non-Post Package, sockets AM2/1207), DDR2.
            .dev_did        = 0x1102,
            .num_dcts       = 1,
            .needs_ext_cfg  = false,
            .select_mode    = AMD_DCT_DIRECT,
            .has_dual_node  = false,
            .has_gang       = true,
            .gang_reg       = AMD_0FH_MODE,
            .gang_bit       = 11,               // F2x90[11] Width128
            .freq_valid_bit = 3,                // F2x94[3] MemClkFreqVal
            .has_freq_chg   = false,            // no FreqChgInProg before Family 10h
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_0FH_DRAM_TIMING,
            .type           = "DDR2",
        },
    };
#endif
    static const struct amd_dct_caps amd_10h_caps[1] = {
        {
            .dev_did        = 0x1202,
            .num_dcts       = 2,
            .needs_ext_cfg  = true,             // DCT1 registers live at 0x100+
            .select_mode    = AMD_DCT_OFFSET,
            .dct_offset     = AMD_10H_DCT_OFFSET,
            .has_dual_node  = true,             // dual-die G34 packages (Opteron 61xx)
            .dual_node_bit  = 29,               // F3xE8[29] MultiNodeCpu
            .has_gang       = true,
            .gang_reg       = AMD_10H_DCT_GANG, // F2x110 = DCT Select Low on F10h
            .gang_bit       = 4,                // F2x110[4] DctGangEn (F10h only)
            .freq_valid_bit = 3,                // F2x94[3] MemClkFreqVal
            .has_freq_chg   = true,
            .freq_chg_bit   = 21,               // F2x94[21] FreqChgInProg
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = true,             // DDR2 or DDR3, from Ddr3Mode
            .timing_reg     = AMD_10H_DRAM_TIMING,
            .type           = "",
        }
    };
#if 0
    static const struct amd_dct_caps amd_11h_caps[1] = {
        { // Family 11h models 00h-0Fh (Griffin/Caspian mobile: Turion X2
          // Ultra, Athlon X2; BKDG 41256 Rev 3.00): the Family 10h register
          // map with DCT1 at offset 0x100 and no DctCfgSel, but DDR2 only
          // (no Ddr3Mode in F2x94, whose [13:8] are reserved) and a 3-bit
          // MemClkFreq. No dual-die packages. Ganged mode exists
          // (F2x110[4] DctGangEn). No FreqChgInProg (F2x94[21] reserved).
            .dev_did        = 0x1302,
            .num_dcts       = 2,
            .needs_ext_cfg  = true,             // DCT1 registers live at 0x100+
            .select_mode    = AMD_DCT_OFFSET,
            .dct_offset     = AMD_11H_DCT_OFFSET,
            .has_dual_node  = false,
            .has_gang       = true,
            .gang_reg       = AMD_11H_DCT_GANG, // F2x110 = DRAM Controller Select Low on F11h
            .gang_bit       = 4,                // F2x110[4] DctGangEn
            .freq_valid_bit = 3,                // F2x94[3] MemClkFreqVal
            .has_freq_chg   = false,            // F2x94[21] reserved (BKDG 41256)
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_11H_DRAM_TIMING,
            .type           = "DDR2",
        }
    };
#endif
#if 0
    // Both Family 12h and 14h expose their DRAM controllers on function 2 of
    // the device 18h northbridge, with the same device ID 1702h (vendor
    // PCI_VID_AMD; shared between Family 12h and Family 14h, like the
    // function 3 MC4 machine-check registers).
    static const struct amd_dct_caps amd_12h_caps[1] = {
        { // Family 12h models 00h-0Fh (Llano A-Series APUs, BKDG 41131
          // Rev 3.02): the Family 10h register map with DCT1 at offset
          // 0x100 and no DctCfgSel. DDR3 only. No dual-die packages. No
          // ganged mode (no DctGangEn), and no FreqChgInProg (F2x94[21]
          // reserved), so only MemClkFreqVal gates the read. Tcl lives in
          // F2x88 and the row timings in the DCT extra data array.
            .dev_did        = 0x1702,
            .num_dcts       = 2,
            .needs_ext_cfg  = true,             // DCT1 registers live at 0x100+
            .select_mode    = AMD_DCT_OFFSET,
            .dct_offset     = AMD_12H_14H_DCT_OFFSET,
            .has_dual_node  = false,
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = false,            // F2x94[21] reserved (BKDG 41131)
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_12H_14H_DRAM_TIMING,
            .row_timing_idx = AMD_12H_14H_DCT_TIMING0,
            .type           = "DDR3",
        },
    };

    static const struct amd_dct_caps amd_14h_caps[1] = {
        { // Family 14h models 00h-0Fh (Bobcat C-/E-/Z-Series APUs, BKDG
          // 43170 Rev 3.13): a single DCT at the base register offsets,
          // like Family 16h. DDR3 only. All the registers this reader
          // touches live below offset 0x100, so no extended config
          // cycles are needed. Same Tcl (F2x88) and row timing (DCT
          // extra data array) arrangement as Family 12h.
            .dev_did        = 0x1702,
            .num_dcts       = 1,
            .needs_ext_cfg  = false,            // all registers below 0x100
            .select_mode    = AMD_DCT_DIRECT,
            .has_dual_node  = false,
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = false,            // F2x94[21] reserved (BKDG 43170)
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_12H_14H_DRAM_TIMING,
            .row_timing_idx = AMD_12H_14H_DCT_TIMING0,
            .type           = "DDR3",
        },
    };
#endif
    static const struct amd_dct_caps amd_15h_caps[5] = {
        { // Models 00h-0Fh: DCT1 selected by F1x10C[2:0] = 1; dual-die packages
          // flagged by F3xE8[29] MultiNodeCpu (G34r1). DDR3 only.
            .dev_did        = 0x1602,
            .num_dcts       = 2,
            .needs_ext_cfg  = true,             // F2x200 lives above offset 0xFF
            .select_mode    = AMD_DCT_CFG_SEL,
            .select_reg     = AMD_15H_DCT_SELECT,
            .select_mask    = 3,                // F1x10C[2:0] DctCfgSel
            .dct1_select    = 1,
            .has_dual_node  = true,
            .dual_node_bit  = 29,               // F3xE8[29] MultiNodeCpu
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = true,
            .freq_chg_bit   = 21,               // F2x94[21] FreqChgInProg
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_15H_DRAM_TIMING,
            .type           = "DDR3",
        },
        { // Models 10h-1Fh (Trinity/Richland, BKDG 42300): DctCfgSel is bit 0
          // of F1x10C (0 = DCT0, 1 = DCT1); Linux f15h_select_dct keeps the
          // same [2:0] mask and value 1 as models 00h-0Fh. No dual-die
          // packages. DDR3 only.
            .dev_did        = 0x1402,
            .num_dcts       = 2,
            .needs_ext_cfg  = true,             // F2x200 lives above offset 0xFF
            .select_mode    = AMD_DCT_CFG_SEL,
            .select_reg     = AMD_15H_DCT_SELECT,
            .select_mask    = 3,                // F1x10C[2:0] DctCfgSel
            .dct1_select    = 1,
            .has_dual_node  = false,
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = true,
            .freq_chg_bit   = 21,               // F2x94[21] FreqChgInProg
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_15H_DRAM_TIMING,
            .type           = "DDR3",
        },
        { // Models 30h-3Fh (Kaveri, BKDG 49125): DctCfgSel[2:0] is an
          // identity encoding (DCT0 = 0..2, DCT1 = 3), so DCT1 is selected
          // by the value 3 - the mapping Linux f15h_select_dct uses
          // (dct = (dct && model == 0x30) ? 3 : dct). No dual-die packages.
            .dev_did        = 0x141C,
            .num_dcts       = 2,
            .needs_ext_cfg  = true,             // F2x200 lives above offset 0xFF
            .select_mode    = AMD_DCT_CFG_SEL,
            .select_reg     = AMD_15H_DCT_SELECT,
            .select_mask    = 3,                // F1x10C[2:0] DctCfgSel
            .dct1_select    = 3,
            .has_dual_node  = false,
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = true,
            .freq_chg_bit   = 21,               // F2x94[21] FreqChgInProg
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_15H_DRAM_TIMING,
            .type           = "DDR3",
        },
        { // Models 60h-6Fh (Carrizo, BKDG 50742): DctCfgSel F1x10C[2:0] is
          // an identity encoding again (000b = DCT0, 001b = DCT1, 010b-111b
          // reserved), and the DCT registers stay in config space - the
          // SMU owns clock training, not the DCT map. Two DCTs, no
          // dual-die packages, no ganged mode. F2x94[21] FreqChgInProg is
          // reserved (unlike Kaveri/Mullins), so only MemClkFreqVal gates
          // the read. The DCT speaks DDR3 or DDR4, selected by F2x78[10:8]
          // DramType - 010b is the first DDR4 encoding in the pre-Zen
          // families - and both types share the F2x200 timing layout.
            .dev_did        = 0x1572,
            .num_dcts       = 2,
            .needs_ext_cfg  = true,             // F2x200 lives above offset 0xFF
            .select_mode    = AMD_DCT_CFG_SEL,
            .select_reg     = AMD_15H_DCT_SELECT,
            .select_mask    = 7,                // F1x10C[2:0] DctCfgSel
            .dct1_select    = 1,
            .has_dual_node  = false,
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = false,            // F2x94[21] reserved (BKDG 50742)
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = true,             // both DCTs must agree on DDR3/DDR4
            .timing_reg     = AMD_15H_DRAM_TIMING,
            .dram_type_reg  = 0x78,             // F2x78[10:8] DramType: 000b DDR3, 010b DDR4
            .type           = "",               // DDR3 or DDR4, from DramType
            .type_alt       = "DDR4",
        },
        { // Models 70h-7Fh (Bristol Ridge, Stoney Ridge, BKDG 55072): a
          // single DCT, like Family 16h - DctCfgSel F1x10C[2:0] is
          // hardwired to 000b (001b-111b reserved), so there is no DCT
          // selection to save and restore. F2x94[21] is reserved like on
          // Carrizo, and F2x78[10:8] DramType and the F2x200 timing
          // layout are the same, so DDR3 and DDR4 are covered alike.
          // Unlike Carrizo, these models have no usable MC4 (F3x44[22]
          // EccEn is reserved), so they get reading support only.
            .dev_did        = 0x15B2,
            .num_dcts       = 1,
            .needs_ext_cfg  = true,             // F2x200 lives above offset 0xFF
            .select_mode    = AMD_DCT_DIRECT,
            .has_dual_node  = false,
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = false,            // F2x94[21] reserved (BKDG 55072)
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_15H_DRAM_TIMING,
            .dram_type_reg  = 0x78,             // F2x78[10:8] DramType: 000b DDR3, 010b DDR4
            .type           = "",               // DDR3 or DDR4, from DramType
            .type_alt       = "DDR4",
        },
    };

    static const struct amd_dct_caps amd_16h_caps[2] = {
        { // Models 00h-0Fh (Kabini, BKDG 48751): a single DCT, at the base
          // offsets; DctCfgSel F1x10C[2:0] is hardwired to 000b. No
          // dual-die packages, no ganged mode. DDR3 only.
            .dev_did        = 0x1532,
            .num_dcts       = 1,
            .needs_ext_cfg  = true,             // F2x200 lives above offset 0xFF
            .select_mode    = AMD_DCT_DIRECT,
            .has_dual_node  = false,
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = true,
            .freq_chg_bit   = 21,               // F2x94[21] FreqChgInProg
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_15H_DRAM_TIMING,
            .type           = "DDR3",
        },
        { // Models 30h-3Fh (Mullins, BKDG 52740): the same die and register
          // map as Kabini, with a different function 2 device ID. DDR3 only.
            .dev_did        = 0x1582,
            .num_dcts       = 1,
            .needs_ext_cfg  = true,             // F2x200 lives above offset 0xFF
            .select_mode    = AMD_DCT_DIRECT,
            .has_dual_node  = false,
            .has_gang       = false,
            .freq_valid_bit = 7,                // F2x94[7] MemClkFreqVal
            .has_freq_chg   = true,
            .freq_chg_bit   = 21,               // F2x94[21] FreqChgInProg
            .has_dis_iface  = true,             // F2x94[14] DisDramInterface
            .type_by_ddr3   = false,
            .timing_reg     = AMD_15H_DRAM_TIMING,
            .type           = "DDR3",
        },
    };
#if 0
    if (imc_family == IMC_K8) {
        // Revision F (Non-Post Package) changed the DCT register layout completely:
        // rev F/G parts have an effective model (model + extendedModel << 4)
        // of 0x40 or above, rev B-E parts (e.g. Opteron 250/270, CPUID
        // 00020F12h) are below 0x40.
        unsigned int model = cpuid_info.version.model + (cpuid_info.version.extendedModel << 4);

        get_imc_config_amd_nb_dct(model < 0x40 ? AMD_FAM_0F_PRENPT : AMD_FAM_0F, &amd_0fh_caps[model < 0x40 ? 0 : 1]);
    }
    else
#endif
    if (imc_family == IMC_K10) {
        get_imc_config_amd_nb_dct(AMD_FAM_10, &amd_10h_caps[0]);
    }
#if 0
    else if (imc_family == IMC_K11) {
        get_imc_config_amd_nb_dct(AMD_FAM_11, &amd_11h_caps[0]);
    }
#endif
#if 0
    else if (imc_family == IMC_K12) {
        get_imc_config_amd_nb_dct(AMD_FAM_12, &amd_12h_caps[0]);
    }
    else if (imc_family == IMC_K14) {
        get_imc_config_amd_nb_dct(AMD_FAM_14, &amd_14h_caps[0]);
    }
#endif
    else if (imc_family == IMC_K15) {
        switch (cpuid_info.version.extendedModel) {
          case 0:
            get_imc_config_amd_nb_dct(AMD_FAM_15_00_0F, &amd_15h_caps[0]);
            break;
          case 1:
            get_imc_config_amd_nb_dct(AMD_FAM_15_10_1F, &amd_15h_caps[1]);
            break;
          case 3:
            get_imc_config_amd_nb_dct(AMD_FAM_15_30_3F, &amd_15h_caps[2]);
            break;
          case 6:
            get_imc_config_amd_nb_dct(AMD_FAM_15_60_6F, &amd_15h_caps[3]);
            break;
          case 7:
            get_imc_config_amd_nb_dct(AMD_FAM_15_70_7F, &amd_15h_caps[4]);
            break;
          default:
            break;
        }
    }
    else if (imc_family == IMC_K16) {
        // The effective model picks the device IDs: extendedModel 0 = models
        // 00h-0Fh (Kabini), 3 = models 30h-3Fh (Mullins, same die).
        switch (cpuid_info.version.extendedModel) {
          case 0:
            get_imc_config_amd_nb_dct(AMD_FAM_16, &amd_16h_caps[0]);
            break;
          case 3:
            get_imc_config_amd_nb_dct(AMD_FAM_16, &amd_16h_caps[1]);
            break;
          default:
            break;
        }
    }
    // else do nothing, shouldn't happen anyway.
}
