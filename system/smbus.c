// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester

#include "display.h"

#include "io.h"
#include "tsc.h"
#include "pci.h"
#include "unistd.h"
#include "string.h"
#include "macros.h"

#include "cpuinfo.h"
#include "memctrl.h"
#include "smbus.h"
#include "smbios.h"
#include "jedec_id.h"
#include "hwquirks.h"

#define LINE_SPD        13
#define MAX_SPD_SLOT    8

ram_info ram = { 0, 0, 0, 0, 0, 0, "N/A"};

int smbdev, smbfun;
unsigned short smbusbase = 0;
uint32_t smbus_id = 0;
static uint16_t extra_initial_sleep_for_smb_transaction = 0;

static int8_t spd_page = -1;
static int8_t last_adr = -1;

// Functions Prototypes
static void read_sku(char *sku, uint8_t slot_idx, uint16_t offset, uint8_t max_len);

static void parse_spd_rdram (spd_info *spdi, uint8_t slot_idx);
static void parse_spd_sdram (spd_info *spdi, uint8_t slot_idx);
static void parse_spd_ddr   (spd_info *spdi, uint8_t slot_idx);
static void parse_spd_ddr2  (spd_info *spdi, uint8_t slot_idx);
static void parse_spd_ddr3  (spd_info *spdi, uint8_t slot_idx);
static void parse_spd_ddr4  (spd_info *spdi, uint8_t slot_idx);
static void parse_spd_ddr5  (spd_info *spdi, uint8_t slot_idx);
static void print_spdi(spd_info spdi, uint8_t lidx);

static bool setup_smb_controller(void);
static bool find_smb_controller(uint16_t vid, uint16_t did);

static uint8_t get_spd(uint8_t slot_idx, uint16_t spd_adr);

static bool nv_mcp_get_smb(void);
static bool amd_sb_get_smb(void);
static bool fch_zen_get_smb(void);
static bool piix4_get_smb(uint8_t address);
static bool ich5_get_smb(void);
static bool ali_get_smb(uint8_t address);
static uint8_t ich5_process(void);
static uint8_t ich5_read_spd_byte(uint8_t adr, uint16_t cmd);
static uint8_t nf_read_spd_byte(uint8_t smbus_adr, uint8_t spd_adr);
static uint8_t ali_m1563_read_spd_byte(uint8_t smbus_adr, uint8_t spd_adr);
static uint8_t ali_m1543_read_spd_byte(uint8_t smbus_adr, uint8_t spd_adr);

static inline uint8_t bcd_to_ui8(uint8_t bcd)
{
    return bcd - 6 * (bcd >> 4);
}

void print_smbus_startup_info(void)
{
    uint8_t spdidx = 0, spd_line_idx = 0;

    spd_info curspd;
    ram.freq = 0;
    curspd.isValid = false;

    if (quirk.type & QUIRK_TYPE_SMBUS) {
        quirk.process();
    }

    if (!setup_smb_controller() || smbusbase == 0) {
        return;
    }

    for (spdidx = 0; spdidx < MAX_SPD_SLOT; spdidx++) {

        memset(&curspd, 0, sizeof(curspd));
        curspd.slot_num = spdidx;

        if (get_spd(spdidx, 0) != 0xFF) {
            switch(get_spd(spdidx, 2))
            {
                default:
                    continue;
                case 0x12: // DDR5
                    parse_spd_ddr5(&curspd, spdidx);
                    break;
                case 0x0C: // DDR4
                    parse_spd_ddr4(&curspd, spdidx);
                    break;
                case 0x0B: // DDR3
                    parse_spd_ddr3(&curspd, spdidx);
                    break;
                case 0x08: // DDR2
                    parse_spd_ddr2(&curspd, spdidx);
                    break;
                case 0x07: // DDR
                    parse_spd_ddr(&curspd, spdidx);
                    break;
                case 0x04: // SDRAM
                    parse_spd_sdram(&curspd, spdidx);
                    break;
                case 0x01: // RAMBUS - RDRAM
                    if (get_spd(spdidx, 1) == 8) {
                        parse_spd_rdram(&curspd, spdidx);
                    }
                    break;
            }

            if (curspd.isValid) {
                if (spd_line_idx == 0) {
                    prints(LINE_SPD-2, 0, "Memory SPD Information");
                    prints(LINE_SPD-1, 0, "----------------------");
                }

                print_spdi(curspd, spd_line_idx);
                spd_line_idx++;
            }
        }
    }
}

static void print_spdi(spd_info spdi, uint8_t lidx)
{
    uint8_t curcol;
    uint16_t i;

    // Print Slot Index, Module Size, type & Max frequency (Jedec or XMP)
    curcol = printf(LINE_SPD+lidx, 0, " - Slot %i: %kB %s-%i",
                    spdi.slot_num,
                    spdi.module_size * 1024,
                    spdi.type,
                    spdi.freq);

    // Print ECC status
    if (spdi.hasECC) {
        curcol = prints(LINE_SPD+lidx, ++curcol, "ECC");
    }

    // Print XMP/EPP Status
    if (spdi.XMP > 0 && spdi.XMP < 20) {
        curcol = prints(LINE_SPD+lidx, ++curcol, "XMP");
    } else if (spdi.XMP == 20) {
        curcol = prints(LINE_SPD+lidx, ++curcol, "EPP");
    }

    // Print Manufacturer from JEDEC106
    for (i = 0; i < JEP106_CNT; i++) {
        if (spdi.jedec_code == jep106[i].jedec_code) {
            curcol = printf(LINE_SPD+lidx, ++curcol, "- %s", jep106[i].name);
            break;
        }
    }

    // If not present in JEDEC106, display raw JEDEC ID
    if (spdi.jedec_code == 0) {
        curcol = prints(LINE_SPD+lidx, ++curcol, "- Noname");
    } else if (i == JEP106_CNT) {
        curcol = printf(LINE_SPD+lidx, ++curcol, "- Unknown (0x%x)", spdi.jedec_code);
    }

    // Print SKU
    if (*spdi.sku)
        curcol = prints(LINE_SPD+lidx, curcol + 1, spdi.sku);

    // Check manufacturing date and print if valid.
    // fab_year is uint8_t and carries only the last two digits.
    //  - for 0..31 we assume 20xx, and 0 means 2000.
    //  - for 96..99 we assume 19xx.
    //  - values 32..95 and > 99 are considered invalid.
    if (curcol <= 69 && spdi.fab_week <= 53 && spdi.fab_week != 0 &&
        (spdi.fab_year < 32 || (spdi.fab_year >= 96 && spdi.fab_year <= 99))) {
        curcol = printf(LINE_SPD+lidx, ++curcol, "(%02i%02i-W%02i)",
                        spdi.fab_year >= 96 ? 19 : 20,
                        spdi.fab_year, spdi.fab_week);
    }

    // Populate global ram var
    ram.type = spdi.type;
    if (ram.freq == 0 || ram.freq > spdi.freq) {
        ram.freq = spdi.freq;
    }
    if (ram.tCL < spdi.tCL) {
        ram.tCL     = spdi.tCL;
        ram.tCL_dec = spdi.tCL_dec;
        ram.tRCD    = spdi.tRCD;
        ram.tRP     = spdi.tRP;
        ram.tRAS    = spdi.tRAS;
    }
}

static void read_sku(char *sku, uint8_t slot_idx, uint16_t offset, uint8_t max_len)
{
    uint8_t sku_len;

    if (max_len > SPD_SKU_LEN)
        max_len = SPD_SKU_LEN;

    for (sku_len = 0; sku_len < max_len; sku_len++) {
        uint8_t sku_byte = get_spd(slot_idx, offset + sku_len);

        // Stop on the first non-ASCII char
        if (sku_byte < 0x20 || sku_byte > 0x7F)
            break;

        sku[sku_len] = (char)sku_byte;
    }

    // Trim spaces from the end
    while (sku_len && sku[sku_len - 1] == 0x20)
        sku_len--;

    sku[sku_len] = '\0';
}

static void parse_spd_ddr5(spd_info *spdi, uint8_t slot_idx)
{
    spdi->type = "DDR5";

    // Compute module size for symmetric & asymmetric configuration
    for (int sbyte_adr = 1; sbyte_adr <= 2; sbyte_adr++) {
        uint32_t cur_rank = 0;
        uint8_t sbyte = get_spd(slot_idx, sbyte_adr * 4);

        // SDRAM Density per die
        switch (sbyte & 0x1F)
        {
          default:
            break;
          case 0b00001:
            cur_rank = 512;
            break;
          case 0b00010:
            cur_rank = 1024;
            break;
          case 0b00011:
            cur_rank = 1536;
            break;
          case 0b00100:
            cur_rank = 2048;
            break;
          case 0b00101:
            cur_rank = 3072;
            break;
          case 0b00110:
            cur_rank = 4096;
            break;
          case 0b00111:
            cur_rank = 6144;
            break;
          case 0b01000:
            cur_rank = 8192;
            break;
        }

        // Die per package
        if ((sbyte >> 5) > 1 && (sbyte >> 5) <= 5) {
            cur_rank *= 1U << (((sbyte >> 5) & 7) - 1);
        }

        sbyte = get_spd(slot_idx, 235);
        spdi->hasECC = (((sbyte >> 3) & 3) > 0);

        // Channels per DIMM
        if (((sbyte >> 5) & 3) == 1) {
            cur_rank *= 2;
        }

        // Primary Bus Width per Channel
        cur_rank *= 1U << ((sbyte & 3) + 3);

        // I/O Width
        sbyte = get_spd(slot_idx, (sbyte_adr * 4) + 2);
        cur_rank /= 1U << (((sbyte >> 5) & 3) + 2);

        sbyte = get_spd(slot_idx, 234);

        // Package ranks per Channel
        cur_rank *= 1U << ((sbyte >> 3) & 7);

        // Add current rank to total package size
        spdi->module_size += cur_rank;

        // If not Asymmetrical, don't process the second rank
        if ((sbyte >> 6) == 0) {
            break;
        }
    }

    // Compute Frequency (including XMP)
    uint16_t tCK, tCKtmp, tns;
    int xmp_offset = 0;

    spdi->XMP = ((get_spd(slot_idx, 640) == 0x0C && get_spd(slot_idx, 641) == 0x4A)) ? 3 : 0;

    if (spdi->XMP == 3) {
        // XMP 3.0 (enumerate all profiles to find the fastest)
        tCK = 0;
        for (int offset = 0; offset < 3*64; offset += 64) {
            tCKtmp = get_spd(slot_idx, 710 + offset) << 8 |
                     get_spd(slot_idx, 709 + offset);

            if (tCKtmp != 0 && (tCK == 0 || tCKtmp < tCK)) {
                xmp_offset = offset;
                tCK = tCKtmp;
            }
        }
    } else {
        // JEDEC
        tCK = get_spd(slot_idx, 21) << 8 |
              get_spd(slot_idx, 20);
    }

    if (tCK == 0) {
        return;
    }

    spdi->freq = (float)(1.0f / tCK * 2.0f * 1000.0f * 1000.0f);
    spdi->freq = (spdi->freq + 50) / 100 * 100;

    // Module Timings
    if (spdi->XMP == 3) {
        // ------------------
        // XMP Specifications
        // ------------------

        // CAS# Latency
        tns  = (uint16_t)get_spd(slot_idx, 718 + xmp_offset) << 8 |
               (uint16_t)get_spd(slot_idx, 717 + xmp_offset);
        spdi->tCL = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;
        spdi->tCL += spdi->tCL % 2; // if tCL is odd, round to upper even.

        // RAS# to CAS# Latency
        tns  = (uint16_t)get_spd(slot_idx, 720 + xmp_offset) << 8 |
               (uint16_t)get_spd(slot_idx, 719 + xmp_offset);
        spdi->tRCD = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;

        // RAS# Precharge
        tns  = (uint16_t)get_spd(slot_idx, 722 + xmp_offset) << 8 |
               (uint16_t)get_spd(slot_idx, 721 + xmp_offset);
        spdi->tRP = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;

        // Row Active Time
        tns  = (uint16_t)get_spd(slot_idx, 724 + xmp_offset) << 8 |
               (uint16_t)get_spd(slot_idx, 723 + xmp_offset);
        spdi->tRAS = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;

        // Row Cycle Time
        tns  = (uint16_t)get_spd(slot_idx, 726 + xmp_offset) << 8 |
               (uint16_t)get_spd(slot_idx, 725 + xmp_offset);
        spdi->tRC = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;
    } else {
        // --------------------
        // JEDEC Specifications
        // --------------------

        // CAS# Latency
        tns  = (uint16_t)get_spd(slot_idx, 31) << 8 |
               (uint16_t)get_spd(slot_idx, 30);
        spdi->tCL = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;
        spdi->tCL += spdi->tCL % 2;

        // RAS# to CAS# Latency
        tns  = (uint16_t)get_spd(slot_idx, 33) << 8 |
               (uint16_t)get_spd(slot_idx, 32);
        spdi->tRCD = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;

        // RAS# Precharge
        tns  = (uint16_t)get_spd(slot_idx, 35) << 8 |
               (uint16_t)get_spd(slot_idx, 34);
        spdi->tRP = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;

        // Row Active Time
        tns  = (uint16_t)get_spd(slot_idx, 37) << 8 |
               (uint16_t)get_spd(slot_idx, 36);
        spdi->tRAS = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;

        // Row Cycle Time
        tns  = (uint16_t)get_spd(slot_idx, 39) << 8 |
               (uint16_t)get_spd(slot_idx, 38);
        spdi->tRC = (tns + tCK - DDR5_ROUNDING_FACTOR) / tCK;
    }

    // Module manufacturer
    spdi->jedec_code = (get_spd(slot_idx, 512) & 0x1F) << 8;
    spdi->jedec_code |= get_spd(slot_idx, 513) & 0x7F;

    read_sku(spdi->sku, slot_idx, 521, 30);

    spdi->fab_year = bcd_to_ui8(get_spd(slot_idx, 515));
    spdi->fab_week = bcd_to_ui8(get_spd(slot_idx, 516));

    spdi->isValid = true;
}

static void parse_spd_ddr4(spd_info *spdi, uint8_t slot_idx)
{
    spdi->type = "DDR4";

    // Compute module size in MB with shifts
    spdi->module_size = 1U << (
                               ((get_spd(slot_idx, 4) & 0xF) + 5)   +  // Total SDRAM capacity: (256 Mbits << byte4[3:0] with an oddity for values >= 8) / 1 KB
                               ((get_spd(slot_idx, 13) & 0x7) + 3)  -  // Primary Bus Width: 8 << byte13[2:0]
                               ((get_spd(slot_idx, 12) & 0x7) + 2)  +  // SDRAM Device Width: 4 << byte12[2:0]
                               ((get_spd(slot_idx, 12) >> 3) & 0x7) +  // Number of Ranks: byte12[5:3]
                               ((get_spd(slot_idx, 6) >> 4) & 0x7)     // Die count - 1: byte6[6:4]
                              );

    spdi->hasECC = (((get_spd(slot_idx, 13) >> 3) & 1) == 1);

    // Module max clock
    float tns, tCK, ramfreq, fround;

    if (get_spd(slot_idx, 384) == 0x0C && get_spd(slot_idx, 385) == 0x4A) {
        // Max XMP
        tCK = (uint8_t)get_spd(slot_idx, 396) * 0.125f +
              (int8_t)get_spd(slot_idx, 431)  * 0.001f;

        spdi->XMP = 2;

    } else {
        // Max JEDEC
        tCK = (uint8_t)get_spd(slot_idx, 18) * 0.125f +
              (int8_t)get_spd(slot_idx, 125) * 0.001f;
    }

    ramfreq = 1.0f / tCK * 2.0f * 1000.0f;

    // Round DRAM Freq to nearest x00/x33/x66
    fround = ((int)(ramfreq * 0.01 + .5) / 0.01) - ramfreq;
    ramfreq += fround;

    if (fround < -16.5) {
        ramfreq += 33;
    } else if (fround > 16.5) {
        ramfreq -= 34;
    }

    spdi->freq = ramfreq;

    // Module Timings
    if (spdi->XMP == 2) {
        // ------------------
        // XMP Specifications
        // ------------------

        // CAS# Latency
        tns  = (uint8_t)get_spd(slot_idx, 401) * 0.125f +
               (int8_t)get_spd(slot_idx, 430)  * 0.001f;
        spdi->tCL = (uint16_t)(tns/tCK + ROUNDING_FACTOR);

        // RAS# to CAS# Latency
        tns  = (uint8_t)get_spd(slot_idx, 402) * 0.125f +
               (int8_t)get_spd(slot_idx, 429)  * 0.001f;
        spdi->tRCD = (uint16_t)(tns/tCK + ROUNDING_FACTOR);

        // RAS# Precharge
        tns  = (uint8_t)get_spd(slot_idx, 403) * 0.125f +
               (int8_t)get_spd(slot_idx, 428)  * 0.001f;
        spdi->tRP = (uint16_t)(tns/tCK + ROUNDING_FACTOR);

        // Row Active Time
        tns = (uint8_t)get_spd(slot_idx, 405) * 0.125f +
              (int8_t)get_spd(slot_idx, 427)  * 0.001f  +
              (uint8_t)(get_spd(slot_idx, 404) & 0x0F) * 32.0f;
        spdi->tRAS = (uint16_t)(tns/tCK + ROUNDING_FACTOR);

        // Row Cycle Time
        tns = (uint8_t)get_spd(slot_idx, 406) * 0.125f +
              (uint8_t)(get_spd(slot_idx, 404) >> 4) * 32.0f;
        spdi->tRC = (uint16_t)(tns/tCK + ROUNDING_FACTOR);
    } else {
        // --------------------
        // JEDEC Specifications
        // --------------------

        // CAS# Latency
        tns  = (uint8_t)get_spd(slot_idx, 24) * 0.125f +
               (int8_t)get_spd(slot_idx, 123) * 0.001f;
        spdi->tCL = (uint16_t)(tns/tCK + ROUNDING_FACTOR);

        // RAS# to CAS# Latency
        tns  = (uint8_t)get_spd(slot_idx, 25) * 0.125f +
               (int8_t)get_spd(slot_idx, 122) * 0.001f;
        spdi->tRCD = (uint16_t)(tns/tCK + ROUNDING_FACTOR);

        // RAS# Precharge
        tns  = (uint8_t)get_spd(slot_idx, 26) * 0.125f +
               (int8_t)get_spd(slot_idx, 121) * 0.001f;
        spdi->tRP = (uint16_t)(tns/tCK + ROUNDING_FACTOR);

        // Row Active Time
        tns = (uint8_t)get_spd(slot_idx, 28) * 0.125f +
              (uint8_t)(get_spd(slot_idx, 27) & 0x0F) * 32.0f;
        spdi->tRAS = (uint16_t)(tns/tCK + ROUNDING_FACTOR);

        // Row Cycle Time
        tns = (uint8_t)get_spd(slot_idx, 29) * 0.125f +
              (uint8_t)(get_spd(slot_idx, 27) >> 4) * 32.0f;
        spdi->tRC = (uint16_t)(tns/tCK + ROUNDING_FACTOR);
    }

    // Module manufacturer
    spdi->jedec_code  = ((uint16_t)(get_spd(slot_idx, 320) & 0x1F)) << 8;
    spdi->jedec_code |= get_spd(slot_idx, 321) & 0x7F;

    read_sku(spdi->sku, slot_idx, 329, 20);

    spdi->fab_year = bcd_to_ui8(get_spd(slot_idx, 323));
    spdi->fab_week = bcd_to_ui8(get_spd(slot_idx, 324));

    spdi->isValid = true;
}

static void parse_spd_ddr3(spd_info *spdi, uint8_t slot_idx)
{
    spdi->type = "DDR3";

    // Compute module size in MB with shifts
    spdi->module_size = 1U << (
                               ((get_spd(slot_idx, 4) & 0xF) + 5)  +  // Total SDRAM capacity: (256 Mbits << byte4[3:0]) / 1 KB
                               ((get_spd(slot_idx, 8) & 0x7) + 3)  -  // Primary Bus Width: 8 << byte8[2:0]
                               ((get_spd(slot_idx, 7) & 0x7) + 2)     // SDRAM Device Width: 4 << byte7[2:0]
                              );

    spdi->module_size *= ((get_spd(slot_idx, 7) >> 3) & 0x7) + 1;     // Number of Ranks: byte7[5:3]

    spdi->hasECC = (((get_spd(slot_idx, 8) >> 3) & 1) == 1);

    uint8_t tck = get_spd(slot_idx, 12);
    uint8_t tck2 = get_spd(slot_idx, 221);

    if (get_spd(slot_idx, 176) == 0x0C && get_spd(slot_idx, 177) == 0x4A) {
        tck = get_spd(slot_idx, 186);

        // Check if profile #2 is faster
        if (tck2 > 5 && tck2 < tck) {
            tck = tck2;
        }

        spdi->XMP = 1;
    }

    // Module jedec speed
    switch (tck) {
        default:
            spdi->freq = 0;
            break;
        case 20:
            spdi->freq = 800;
            break;
        case 15:
            spdi->freq = 1066;
            break;
        case 12:
            spdi->freq = 1333;
            break;
        case 10:
            spdi->freq = 1600;
            break;
        case 9:
            spdi->freq = 1866;
            break;
        case 8:
            spdi->freq = 2133;
            break;
        case 7:
            spdi->freq = 2400;
            break;
        case 6:
            spdi->freq = 2666;
            break;
    }


    // Module Timings
    float tckns, tns, mtb;

    if (spdi->XMP == 1) {
        // ------------------
        // XMP Specifications
        // ------------------
        float mtb_dividend = get_spd(slot_idx, 180);
        float mtb_divisor = get_spd(slot_idx, 181);

        mtb = (mtb_divisor == 0) ? 0.125f : mtb_dividend / mtb_divisor;

        tckns = (float)get_spd(slot_idx, 186);

        // XMP Draft with non-standard MTB divisors (!= 0.125)
        if (mtb_divisor == 12.0f && tckns == 10.0f) {
            spdi->freq = 2400;
        } else if (mtb_divisor == 14.0f && tckns == 15.0f) {
            spdi->freq = 1866;
        }

        if (spdi->freq >= 1866 && mtb_divisor == 8.0f) {
            tckns -= 0.4f;
        }

        tckns *= mtb;

        // CAS# Latency
        tns  = get_spd(slot_idx, 187);
        spdi->tCL = (uint16_t)((tns*mtb)/tckns + ROUNDING_FACTOR);

        // RAS# to CAS# Latency
        tns  = get_spd(slot_idx, 192);
        spdi->tRCD = (uint16_t)((tns*mtb)/tckns + ROUNDING_FACTOR);

        // RAS# Precharge
        tns  = get_spd(slot_idx, 191);
        spdi->tRP = (uint16_t)((tns*mtb)/tckns + ROUNDING_FACTOR);

        // Row Active Time
        tns  = (uint16_t)((get_spd(slot_idx, 194) & 0x0F) << 8 |
               get_spd(slot_idx, 195));

        spdi->tRAS = (uint16_t)((tns*mtb)/tckns + ROUNDING_FACTOR);

        // Row Cycle Time
        tns  = (uint16_t)((get_spd(slot_idx, 194) & 0xF0) << 4 |
               get_spd(slot_idx, 196));
        spdi->tRC = (uint16_t)((tns*mtb)/tckns + ROUNDING_FACTOR);
    } else {
        // --------------------
        // JEDEC Specifications
        // --------------------
        mtb = 0.125f;

        tckns = (uint8_t)get_spd(slot_idx, 12) * mtb +
                (int8_t)get_spd(slot_idx, 34) * 0.001f;

        // CAS# Latency
        tns  = (uint8_t)get_spd(slot_idx, 16) * mtb +
               (int8_t)get_spd(slot_idx, 35) * 0.001f;
        spdi->tCL = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

        // RAS# to CAS# Latency
        tns  = (uint8_t)get_spd(slot_idx, 18) * mtb +
               (int8_t)get_spd(slot_idx, 36) * 0.001f;
        spdi->tRCD = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

        // RAS# Precharge
        tns  = (uint8_t)get_spd(slot_idx, 20) * mtb +
               (int8_t)get_spd(slot_idx, 37) * 0.001f;
        spdi->tRP = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

        // Row Active Time
        tns = (uint8_t)get_spd(slot_idx, 22) * mtb +
              (uint8_t)(get_spd(slot_idx, 21) & 0x0F) * 32.0f;
        spdi->tRAS = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

        // Row Cycle Time
        tns = (uint8_t)get_spd(slot_idx, 23) * mtb +
              (uint8_t)(get_spd(slot_idx, 21) >> 4) * 32.0f + 1;
        spdi->tRC = (uint16_t)(tns/tckns  + ROUNDING_FACTOR);
    }

    // Module manufacturer
    spdi->jedec_code  = ((uint16_t)(get_spd(slot_idx, 117) & 0x1F)) << 8;
    spdi->jedec_code |= get_spd(slot_idx, 118) & 0x7F;

    read_sku(spdi->sku, slot_idx, 128, 18);

    spdi->fab_year = bcd_to_ui8(get_spd(slot_idx, 120));
    spdi->fab_week = bcd_to_ui8(get_spd(slot_idx, 121));

    spdi->isValid = true;
}

static void parse_spd_ddr2(spd_info *spdi, uint8_t slot_idx)
{
    spdi->type = "DDR2";

    // Compute module size in MB
    switch (get_spd(slot_idx, 31)) {
        case 1:
            spdi->module_size = 1024;
            break;
        case 2:
            spdi->module_size = 2048;
            break;
        case 4:
            spdi->module_size = 4096;
            break;
        case 8:
            spdi->module_size = 8192;
            break;
        case 16:
            spdi->module_size = 16384;
            break;
        case 32:
            spdi->module_size = 128;
            break;
        case 64:
            spdi->module_size = 256;
            break;
        default:
        case 128:
            spdi->module_size = 512;
            break;
    }

    spdi->module_size *= (get_spd(slot_idx, 5) & 7) + 1;

    spdi->hasECC = ((get_spd(slot_idx, 11) >> 1) == 1);

    float tckns, tns;
    uint8_t tbyte;

    // Module EPP Detection (we only support Full profiles)
    uint8_t epp_offset = 0;
    if (get_spd(slot_idx, 99) == 0x6D && get_spd(slot_idx, 102) == 0xB1) {
        epp_offset = (get_spd(slot_idx, 103) & 0x3) * 12;
        tbyte = get_spd(slot_idx, 109 + epp_offset);
        spdi->XMP = 20;
    } else {
        tbyte = get_spd(slot_idx, 9);
    }

    // Module speed
    tckns = (tbyte & 0xF0) >> 4;
    tbyte &= 0xF;

    if (tbyte < 10) {
        tckns += (tbyte & 0xF) * 0.1f;
    } else if (tbyte == 10) {
        tckns += 0.25f;
    } else if (tbyte == 11) {
        tckns += 0.33f;
    } else if (tbyte == 12) {
        tckns += 0.66f;
    } else if (tbyte == 13) {
        tckns += 0.75f;
    } else if (tbyte == 14) { // EPP Specific
        tckns += 0.875f;
    }

    spdi->freq = (float)(1.0f / tckns * 1000.0f * 2.0f);

    if (spdi->XMP == 20) {
        // Module Timings (EPP)
        // CAS# Latency
        tbyte = get_spd(slot_idx, 110 + epp_offset);
        for (int shft = 0; shft < 7; shft++) {
            if ((tbyte >> shft) & 1) {
                spdi->tCL = shft;
            }
        }

        // RAS# to CAS# Latency
        tbyte = get_spd(slot_idx, 111 + epp_offset);
        tns = ((tbyte & 0xFC) >> 2) + (tbyte & 0x3) * 0.25f;
        spdi->tRCD = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

        // RAS# Precharge
        tbyte = get_spd(slot_idx, 112 + epp_offset);
        tns = ((tbyte & 0xFC) >> 2) + (tbyte & 0x3) * 0.25f;
        spdi->tRP = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

        // Row Active Time
        tns = get_spd(slot_idx, 113 + epp_offset);
        spdi->tRAS = (uint16_t)(tns/tckns + ROUNDING_FACTOR);
    } else {
        // Module Timings (JEDEC)
        // CAS# Latency
        tbyte = get_spd(slot_idx, 18);
        for (int shft = 0; shft < 7; shft++) {
            if ((tbyte >> shft) & 1) {
                spdi->tCL = shft;
            }
        }

        // RAS# to CAS# Latency
        tbyte = get_spd(slot_idx, 29);
        tns = ((tbyte & 0xFC) >> 2) + (tbyte & 0x3) * 0.25f;
        spdi->tRCD = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

        // RAS# Precharge
        tbyte = get_spd(slot_idx, 27);
        tns = ((tbyte & 0xFC) >> 2) + (tbyte & 0x3) * 0.25f;
        spdi->tRP = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

        // Row Active Time
        tns = get_spd(slot_idx, 30);
        spdi->tRAS = (uint16_t)(tns/tckns + ROUNDING_FACTOR);
    }

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi->jedec_code  = ((uint16_t)(contcode - 64)) << 8;
    spdi->jedec_code |= get_spd(slot_idx, contcode) & 0x7F;

    read_sku(spdi->sku, slot_idx, 73, 18);

    spdi->fab_year = bcd_to_ui8(get_spd(slot_idx, 93));
    spdi->fab_week = bcd_to_ui8(get_spd(slot_idx, 94));

    spdi->isValid = true;
}

static void parse_spd_ddr(spd_info *spdi, uint8_t slot_idx)
{
    spdi->type = "DDR";

    // Compute module size in MB
    switch (get_spd(slot_idx, 31)) {
        case 1:
            spdi->module_size = 1024;
            break;
        case 2:
            spdi->module_size = 2048;
            break;
        case 4:
            spdi->module_size = 4096;
            break;
        case 8:
            spdi->module_size = 32;
            break;
        case 16:
            spdi->module_size = 64;
            break;
        case 32:
            spdi->module_size = 128;
            break;
        case 64:
            spdi->module_size = 256;
            break;
        case 128:
            spdi->module_size = 512;
            break;
        default: // we don't support asymmetric banking
            spdi->module_size = 0;
            break;
    }

    spdi->module_size *= get_spd(slot_idx, 5);

    spdi->hasECC = ((get_spd(slot_idx, 11) >> 1) == 1);

    // Module speed
    float tns, tckns;
    uint8_t spd_byte9 = get_spd(slot_idx, 9);
    tckns = (spd_byte9 >> 4) + (spd_byte9 & 0xF) * 0.1f;

    spdi->freq = (uint16_t)(1.0f / tckns * 1000.0f * 2.0f);

    // Module Timings
    uint8_t spd_byte18 = get_spd(slot_idx, 18);
    for (int shft = 0; shft < 7; shft++) {
        if ((spd_byte18 >> shft) & 1) {
            spdi->tCL = 1.0f + shft * 0.5f;
            // Check tCL decimal (x.5 CAS)
            if (shft == 1 || shft == 3 || shft == 5) {
                spdi->tCL_dec = 5;
            } else {
                spdi->tCL_dec = 0;
            }
        }
    }

    tns = (get_spd(slot_idx, 29) >> 2) +
          (get_spd(slot_idx, 29) & 0x3) * 0.25f;
    spdi->tRCD = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

    tns = (get_spd(slot_idx, 27) >> 2) +
          (get_spd(slot_idx, 27) & 0x3) * 0.25f;
    spdi->tRP = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

    spdi->tRAS = (uint16_t)((float)get_spd(slot_idx, 30)/tckns + ROUNDING_FACTOR);

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi->jedec_code = (contcode - 64) << 8;
    spdi->jedec_code |= get_spd(slot_idx, contcode) & 0x7F;

    read_sku(spdi->sku, slot_idx, 73, 18);

    spdi->fab_year = bcd_to_ui8(get_spd(slot_idx, 93));
    spdi->fab_week = bcd_to_ui8(get_spd(slot_idx, 94));

    spdi->isValid = true;
}

static void parse_spd_rdram(spd_info *spdi, uint8_t slot_idx)
{
    spdi->type = "RDRAM";

    // Compute module size in MB
    uint8_t tbyte = get_spd(slot_idx, 5);
    switch(tbyte) {
        case 0x84:
            spdi->module_size = 8;
            break;
        case 0xC5:
            spdi->module_size = 16;
            break;
        default:
            return;
    }

    spdi->module_size *= get_spd(slot_idx, 99);

    tbyte = get_spd(slot_idx, 4);
    if (tbyte > 0x96) {
        spdi->module_size *= 1 + (((tbyte & 0xF0) >> 4) - 9) + ((tbyte & 0xF) - 6);
    }

    spdi->hasECC = (get_spd(slot_idx, 100) == 0x12) ? true : false;

    // Module speed
    tbyte = get_spd(slot_idx, 15);
    switch(tbyte) {
        case 0x1A:
            spdi->freq = 600;
            break;
        case 0x15:
            spdi->freq = 711;
            break;
        case 0x13:
            spdi->freq = 800;
            break;
        case 0xe:
            spdi->freq = 1066;
            break;
        case 0xc:
            spdi->freq = 1200;
            break;
        default:
            return;
    }

    // Module Timings
    spdi->tCL = get_spd(slot_idx, 14);
    spdi->tRCD = get_spd(slot_idx, 12);
    spdi->tRP = get_spd(slot_idx, 10);
    spdi->tRAS = get_spd(slot_idx, 11);

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi->jedec_code  = ((uint16_t)(contcode - 64)) << 8;
    spdi->jedec_code |= get_spd(slot_idx, contcode) & 0x7F;

    read_sku(spdi->sku, slot_idx, 73, 18);

    spdi->fab_year = bcd_to_ui8(get_spd(slot_idx, 93));
    spdi->fab_week = bcd_to_ui8(get_spd(slot_idx, 94));

    spdi->isValid = true;
}

static void parse_spd_sdram(spd_info *spdi, uint8_t slot_idx)
{
    spdi->type = "SDRAM";

    uint8_t spd_byte3  = get_spd(slot_idx, 3) & 0x0F; // Number of Row Addresses (2 x 4 bits, upper part used if asymmetrical banking used)
    uint8_t spd_byte4  = get_spd(slot_idx, 4) & 0x0F; // Number of Column Addresses (2 x 4 bits, upper part used if asymmetrical banking used)
    uint8_t spd_byte5  = get_spd(slot_idx, 5);        // Number of Banks on module (8 bits)
    uint8_t spd_byte17 = get_spd(slot_idx, 17);       // SDRAM Device Attributes, Number of Banks on the discrete SDRAM Device (8 bits)

    // Size in MB
    if (   (spd_byte3 != 0)
        && (spd_byte4 != 0)
        && (spd_byte3 + spd_byte4 > 17)
        && (spd_byte3 + spd_byte4 <= 29)
        && (spd_byte5 <= 8)
        && (spd_byte17 <= 8)
       ) {
        spdi->module_size = (1U << (spd_byte3 + spd_byte4 - 17)) * ((uint16_t)spd_byte5 * spd_byte17);
    } else {
        spdi->module_size = 0;
    }

    spdi->hasECC = ((get_spd(slot_idx, 11) >> 1) == 1);

    // Module speed
    float tns, tckns;
    uint8_t spd_byte9 = get_spd(slot_idx, 9);
    tckns = (spd_byte9 >> 4) + (spd_byte9 & 0xF) * 0.1f;

    spdi->freq = (uint16_t)(1000.0f / tckns);

    // Module Timings
    uint8_t spd_byte18 = get_spd(slot_idx, 18);
    for (int shft = 0; shft < 7; shft++) {
        if ((spd_byte18 >> shft) & 1) {
            spdi->tCL = shft + 1;
        }
    }

    tns = get_spd(slot_idx, 29);
    spdi->tRCD = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

    tns = get_spd(slot_idx, 27);
    spdi->tRP = (uint16_t)(tns/tckns + ROUNDING_FACTOR);

    spdi->tRAS = (uint16_t)(get_spd(slot_idx, 30)/tckns + ROUNDING_FACTOR);

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi->jedec_code  = ((uint16_t)(contcode - 64)) << 8;
    spdi->jedec_code |= get_spd(slot_idx, contcode) & 0x7F;

    read_sku(spdi->sku, slot_idx, 73, 18);

    spdi->fab_year = bcd_to_ui8(get_spd(slot_idx, 93));
    spdi->fab_week = bcd_to_ui8(get_spd(slot_idx, 94));

    spdi->isValid = true;
}


// --------------------------
// SMBUS Controller Functions
// --------------------------

static bool setup_smb_controller(void)
{
    uint16_t vid, did;

    for (smbdev = 0; smbdev < 32; smbdev++) {
        for (smbfun = 0; smbfun < 8; smbfun++) {
            vid = pci_config_read16(0, smbdev, smbfun, 0);
            if (vid != 0xFFFF) {
                did = pci_config_read16(0, smbdev, smbfun, 2);
                if (did != 0xFFFF) {
                    if (find_smb_controller(vid, did)) {
                        return true;
                    }
                }
            }
        }
    }
    smbus_id = 0;
    return false;
}

// ----------------------------------------------------------
// WARNING: Be careful when adding a controller ID!
// Incorrect SMB accesses (ie: on bank switch) can brick your
// motherboard or your memory module.
//                           ----
// No Pull Request including a new SMBUS Controller will be
// accepted without a proof (screenshot) that it has been
// tested successfully on a real motherboard.
// ----------------------------------------------------------

// PCI device IDs for Intel i801 SMBus controller.
static const uint16_t intel_ich5_dids[] =
{
    0x2413,  // 82801AA (ICH)
    0x2423,  // 82801AB (ICH)
    0x2443,  // 82801BA (ICH2)
    0x2483,  // 82801CA (ICH3)
    0x24C3,  // 82801DB (ICH4)
    0x24D3,  // 82801E (ICH5)
    0x25A4,  // 6300ESB
    0x266A,  // 82801F (ICH6)
    0x269B,  // 6310ESB/6320ESB
    0x27DA,  // 82801G (ICH7)
    0x283E,  // 82801H (ICH8)
    0x2930,  // 82801I (ICH9)
    0x5032,  // EP80579 (Tolapai)
    0x3A30,  // ICH10
    0x3A60,  // ICH10
    0x3B30,  // 5/3400 Series (PCH)
    0x1C22,  // 6 Series (PCH)
    0x1D22,  // Patsburg (PCH)
    0x1D70,  // Patsburg (PCH) IDF
    0x1D71,  // Patsburg (PCH) IDF
    0x1D72,  // Patsburg (PCH) IDF
    0x2330,  // DH89xxCC (PCH)
    0x1E22,  // Panther Point (PCH)
    0x8C22,  // Lynx Point (PCH)
    0x9C22,  // Lynx Point-LP (PCH)
    0x1F3C,  // Avoton (SOC)
    0x8D22,  // Wellsburg (PCH)
    0x8D7D,  // Wellsburg (PCH) MS
    0x8D7E,  // Wellsburg (PCH) MS
    0x8D7F,  // Wellsburg (PCH) MS
    0x23B0,  // Coleto Creek (PCH)
    0x8CA2,  // Wildcat Point (PCH)
    0x9CA2,  // Wildcat Point-LP (PCH)
    0x0F12,  // BayTrail (SOC)
    0x2292,  // Braswell (SOC)
    0xA123,  // Sunrise Point-H (PCH)
    0x9D23,  // Sunrise Point-LP (PCH)
    0x19DF,  // Denverton  (SOC)
    0x1BC9,  // Emmitsburg (PCH)
    0xA1A3,  // Lewisburg (PCH)
    0xA223,  // Lewisburg Super (PCH)
    0xA2A3,  // Kaby Lake (PCH-H)
    0x31D4,  // Gemini Lake (SOC)
    0xA323,  // Cannon Lake-H (PCH)
    0x9DA3,  // Cannon Lake-LP (PCH)
    0x18DF,  // Cedar Fork (PCH)
    0x34A3,  // Ice Lake-LP (PCH)
    0x38A3,  // Ice Lake-N (PCH)
    0x02A3,  // Comet Lake (PCH)
    0x06A3,  // Comet Lake-H (PCH)
    0x4B23,  // Elkhart Lake (PCH)
    0xA0A3,  // Tiger Lake-LP (PCH)
    0x43A3,  // Tiger Lake-H (PCH)
    0x4DA3,  // Jasper Lake (SOC)
    0xA3A3,  // Comet Lake-V (PCH)
    0x7AA3,  // Alder Lake-S (PCH)
    0x51A3,  // Alder Lake-P (PCH)
    0x54A3,  // Alder Lake-M (PCH)
    0x7A23,  // Raptor Lake-S (PCH)
};

static bool find_in_did_array(uint16_t did, const uint16_t * ids, unsigned int size)
{
    for (unsigned int i = 0; i < size; i++) {
        if (*ids++ == did) {
            return true;
        }
    }
    return false;
}

static bool find_smb_controller(uint16_t vid, uint16_t did)
{
    smbus_id = (((uint32_t)vid) << 16) | did;
    switch(vid)
    {
        case PCI_VID_INTEL:
        {
            if (find_in_did_array(did, intel_ich5_dids, ARRAY_SIZE(intel_ich5_dids))) {
                return ich5_get_smb();
            }
            if (did == 0x7113) { // 82371AB/EB/MB PIIX4
                return piix4_get_smb(PIIX4_SMB_BASE_ADR_DEFAULT);
            }
            // 0x719B 82440/82443MX PMC - PIIX4
            // 0x0F13 ValleyView SMBus Controller ?
            // 0x8119 US15W ?
            return false;
        }

        case PCI_VID_HYGON:
        case PCI_VID_AMD:
            switch(did)
            {
                // case 0x740B: // AMD756
                // case 0x7413: // AMD766
                // case 0x7443: // AMD768
                // case 0x746B: // AMD8111_SMBUS
                // case 0x746A: // AMD8111_SMBUS2
                case 0x780B: // AMD FCH (Pre-Zen)
                    return amd_sb_get_smb();
                case 0x790B: // AMD FCH (Zen 2/3)
                    return fch_zen_get_smb();
                default:
                return false;
            }
            break;

        case PCI_VID_ATI:
            switch(did)
            {
                // case 0x4353: // SB200
                // case 0x4363: // SB300
                case 0x4372:    // SB400
                    return piix4_get_smb(PIIX4_SMB_BASE_ADR_DEFAULT);
                case 0x4385:    // SB600+
                    return amd_sb_get_smb();
                default:
                    return false;
            }
            break;

        case PCI_VID_NVIDIA:
            switch(did)
            {
                // case 0x01B4: // nForce
                case 0x0064:    // nForce 2
                // case 0x0084: // nForce 2 Mobile
                case 0x00E4:    // nForce 3
                // case 0x0034: // MCP04
                // case 0x0052: // nForce 4
                case 0x0264:    // nForce 410/430 MCP
                case 0x03EB:    // nForce 630a
                // case 0x0446: // nForce 520
                // case 0x0542: // nForce 560
                case 0x0752:    // nForce 720a
                // case 0x07D8: // nForce 630i
                // case 0x0AA2: // nForce 730i
                // case 0x0D79: // MCP89
                case 0x0368:    // nForce 680a/680i/780i/790i
                    return nv_mcp_get_smb();
                default:
                    return false;
            }
            break;

        case PCI_VID_SIS:
            switch(did)
            {
                // case 0x0008:
                    // SiS5595, SiS630 or other SMBus controllers - it's complicated.
                // case 0x0016:
                    // SiS961/2/3, known as "SiS96x" SMBus controllers.
                // case 0x0018:
                // case 0x0964:
                    // SiS630 SMBus controllers.
                default:
                    return false;
            }
            break;

        case PCI_VID_VIA:
            switch(did)
            {
                // case 0x3040: // 82C586_3
                    // via SMBus controller.
                // case 0x3050: // 82C596_3
                    // Try SMB base address = 0x90, then SMB base address = 0x80
                    // viapro SMBus controller, i.e. PIIX4 with a small quirk.
                // case 0x3051: // 82C596B_3
                case 0x3057: // 82C686_4
                // case 0x8235: // 8231_4
                    // SMB base address = 0x90
                    // viapro SMBus controller, i.e. PIIX4.
                    return piix4_get_smb(PIIX4_SMB_BASE_ADR_DEFAULT);
                case 0x3074: // 8233
                case 0x3147: // 8233A
                case 0x3177: // 8235
                case 0x3227: // 8237
                // case 0x3337: // 8237A
                // case 0x3372: // 8237S
                // case 0x3287: // 8251
                // case 0x8324: // CX700
                // case 0x8353: // VX800
                // case 0x8409: // VX855
                // case 0x8410: // VX900
                    // SMB base address = 0xD0
                    // viapro I2C controller, i.e. PIIX4 with a small quirk.
                    return piix4_get_smb(PIIX4_SMB_BASE_ADR_VIAPRO);
                default:
                    return false;
            }
            break;

        case PCI_VID_EFAR:
            switch(did)
            {
                // case 0x9463: // SLC90E66_3: PIIX4
                default:
                    return false;
            }
            break;

        case PCI_VID_ALI:
            switch(did)
            {
                case 0x7101: // ALi M1533/1535/1543C
                    return ali_get_smb(PIIX4_SMB_BASE_ADR_ALI1543);
                case 0x1563: // ALi M1563
                    return piix4_get_smb(PIIX4_SMB_BASE_ADR_ALI1563);
                default:
                    return false;
            }
            break;

        case PCI_VID_SERVERWORKS:
            switch(did)
            {
                case 0x0201: // CSB5
                    // From Linux i2c-piix4 driver: unlike its siblings, this model needs a quirk.
                    extra_initial_sleep_for_smb_transaction = 2100 - 500;
                // Fall through.
                // case 0x0200: // OSB4
                // case 0x0203: // CSB6
                // case 0x0205: // HT1000SB
                // case 0x0408: // HT1100LD
                    return piix4_get_smb(PIIX4_SMB_BASE_ADR_DEFAULT);
                default:
                    return false;
            }
            break;
        default:
            return false;
    }
    return false;
}

// ----------------------
// PIIX4 SMBUS Controller
// ----------------------

static bool piix4_get_smb(uint8_t address)
{
    uint16_t x = pci_config_read16(0, smbdev, smbfun, address) & 0xFFF0;

    if (x != 0) {
        smbusbase = x;
        return true;
    }

    return false;
}

// ----------------------------
// i801 / ICH5 SMBUS Controller
// ----------------------------

static bool ich5_get_smb(void)
{
    uint16_t x;

    // Enable SMBus IO Space if disabled
    x = pci_config_read16(0, smbdev, smbfun, 0x4);

    if (!(x & 1)) {
        pci_config_write16(0, smbdev, smbfun, 0x4, x | 1);
    }

    // Read Base Address
    x = pci_config_read16(0, smbdev, smbfun, 0x20);
    smbusbase = x & 0xFFF0;

    // Enable I2C Host Controller Interface if disabled
    uint8_t temp = pci_config_read8(0, smbdev, smbfun, 0x40);
    if ((temp & 4) == 0) {
        pci_config_write8(0, smbdev, smbfun, 0x40, temp | 0x04);
    }

    // Reset SMBUS Controller
    __outb(__inb(SMBHSTSTS) & 0x1F, SMBHSTSTS);
    usleep(1000);

    return (smbusbase != 0);
}

// --------------------
// AMD SMBUS Controller
// --------------------

static bool amd_sb_get_smb(void)
{
    uint8_t rev_id;
    uint16_t pm_reg;

    rev_id = pci_config_read8(0, smbdev, smbfun, 0x08);

    if ((smbus_id & 0xFFFF) == 0x4385 && rev_id <= 0x3D) {
        // Older AMD SouthBridge (SB700 & older) use PIIX4 registers
        return piix4_get_smb(PIIX4_SMB_BASE_ADR_DEFAULT);
    } else if ((smbus_id & 0xFFFF) == 0x780B && rev_id == 0x42) {
        // Latest Pre-Zen APUs use the newer Zen PM registers
        return fch_zen_get_smb();
    } else {
         // AMD SB (SB800 up to pre-FT3/FP4/AM4) uses specific registers
        __outb(AMD_SMBUS_BASE_REG + 1, AMD_INDEX_IO_PORT);
        pm_reg = __inb(AMD_DATA_IO_PORT) << 8;
        __outb(AMD_SMBUS_BASE_REG, AMD_INDEX_IO_PORT);
        pm_reg |= __inb(AMD_DATA_IO_PORT) & 0xE0;

        if (pm_reg != 0xFFE0 && pm_reg != 0) {
            smbusbase = pm_reg;
            return true;
        }
    }

    return false;
}

static bool fch_zen_get_smb(void)
{
    uint16_t pm_reg;

    __outb(AMD_PM_INDEX + 1, AMD_INDEX_IO_PORT);
    pm_reg = __inb(AMD_DATA_IO_PORT) << 8;
    __outb(AMD_PM_INDEX, AMD_INDEX_IO_PORT);
    pm_reg |= __inb(AMD_DATA_IO_PORT);

    // Special case for AMD Family 19h & Extended Model > 4 (get smb address in memory)
    if ((imc.family == IMC_K19_CZN || imc.family == IMC_K19_RPL) && pm_reg == 0xFFFF) {
        smbusbase = ((*(const uint32_t *)(0xFED80000 + 0x300) >> 8) & 0xFF) << 8;
        return true;
    }

    // Check if IO Smbus is enabled.
    if ((pm_reg & 0x10) == 0) {
        return false;
    }

    if ((pm_reg & 0xFF00) != 0) {
        smbusbase = pm_reg & 0xFF00;
        return true;
    }

    return false;
}

// -----------------------
// nVidia SMBUS Controller
// -----------------------

static bool nv_mcp_get_smb(void)
{
    int smbus_base_adr;

    if ((smbus_id & 0xFFFF) >= 0x200) {
        smbus_base_adr = NV_SMBUS_ADR_REG;
    } else {
        smbus_base_adr = NV_OLD_SMBUS_ADR_REG;
    }

    // nForce SB has 2 I2C Busses. SPD is located on first I2C Bus.
    uint16_t x = pci_config_read16(0, smbdev, smbfun, smbus_base_adr) & 0xFFFC;

    if (x != 0) {
        smbusbase = x;
        return true;
    }

    return false;
}

// ---------------------------------------
// ALi SMBUS Controller (M1533/1535/1543C)
// ---------------------------------------

static bool ali_get_smb(uint8_t address)
{
    // Enable SMB I/O Base Address Register Control (Reg0x5B[2] = 0)
    uint16_t temp = pci_config_read8(0, smbdev, smbfun, 0x5B);
    pci_config_write8(0, smbdev, smbfun, 0x5B, temp & ~0x06);

    // Enable Response to I/O Access. (Reg0x04[0] = 1)
    temp = pci_config_read8(0, smbdev, smbfun, 0x04);
    pci_config_write8(0, smbdev, smbfun, 0x04, temp | 0x01);

    // SMB Host Controller Interface Enable (Reg0xE0[0] = 1)
    temp = pci_config_read8(0, smbdev, smbfun, 0xE0);
    pci_config_write8(0, smbdev, smbfun, 0xE0, temp | 0x01);

    // Read SMBase Register (usually 0xE800)
    uint16_t x = pci_config_read16(0, smbdev, smbfun, address) & 0xFFF0;

    if (x != 0) {
        smbusbase = x;
        return true;
    }

    return false;
}

// ------------------
// get_spd() function
// ------------------

static uint8_t get_spd(uint8_t slot_idx, uint16_t spd_adr)
{
    switch ((smbus_id >> 16) & 0xFFFF) {
      case PCI_VID_ALI:
        if ((smbus_id & 0xFFFF) == 0x7101)
            return ali_m1543_read_spd_byte(slot_idx, (uint8_t)spd_adr);
        else
            return ali_m1563_read_spd_byte(slot_idx, (uint8_t)spd_adr);
      case PCI_VID_NVIDIA:
        return nf_read_spd_byte(slot_idx, (uint8_t)spd_adr);
      default:
        return ich5_read_spd_byte(slot_idx, spd_adr);
    }
}

/*************************************************************************************
/ *****************************         WARNING          *****************************
/ ************************************************************************************
/   Be absolutely sure to know what you're doing before changing the function below!
/       You can easily WRITE into the SPD (especially with DDR5) and corrupt it
/                /!\  Your RAM modules will not work anymore  /!\
/ *************************************************************************************/

static uint8_t ich5_read_spd_byte(uint8_t smbus_adr, uint16_t spd_adr)
{
    smbus_adr += 0x50;

    if (dmi_memory_device->type == DMI_DDR4) {
        // Switch page if needed (DDR4)
        if (spd_adr > 0xFF && spd_page != 1) {
            __outb((0x37 << 1) | I2C_WRITE, SMBHSTADD);
            __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

            ich5_process(); // return should 0x42 or 0x44
            spd_page = 1;

        } else if (spd_adr <= 0xFF && spd_page != 0) {
            __outb((0x36 << 1) | I2C_WRITE, SMBHSTADD);
            __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

            ich5_process();
            spd_page = 0;
        }

        if (spd_adr > 0xFF) {
            spd_adr -= 0x100;
        }
    } else if (dmi_memory_device->type == DMI_DDR5) {
        // Switch page if needed (DDR5)
        uint8_t adr_page = spd_adr / 128;

        if (adr_page != spd_page || last_adr != smbus_adr) {
            __outb((smbus_adr << 1) | I2C_WRITE, SMBHSTADD);
            __outb(SPD5_MR11 & 0x7F, SMBHSTCMD);
            __outb(adr_page, SMBHSTDAT0);
            __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

            ich5_process();

            spd_page = adr_page;
            last_adr = smbus_adr;
         }

          spd_adr -= adr_page * 128;
          spd_adr |= 0x80;
    }

    __outb((smbus_adr << 1) | I2C_READ, SMBHSTADD);
    __outb(spd_adr, SMBHSTCMD);
    __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

    if (ich5_process() == 0) {
        return __inb(SMBHSTDAT0);
    } else {
        return 0xFF;
    }
}

static uint8_t ich5_process(void)
{
    uint8_t status;
    uint16_t timeout = 0;

    status = __inb(SMBHSTSTS) & 0x1F;

    if (status != 0x00) {
        __outb(status, SMBHSTSTS);
        usleep(500);
        if ((status = (0x1F & __inb(SMBHSTSTS))) != 0x00) {
            return 1;
        }
    }

    __outb(__inb(SMBHSTCNT) | SMBHSTCNT_START, SMBHSTCNT);

    // Some SMB controllers need this quirk.
    if (extra_initial_sleep_for_smb_transaction) {
        usleep(extra_initial_sleep_for_smb_transaction);
    }

    do {
        usleep(500);
        status = __inb(SMBHSTSTS);
    } while ((status & 0x01) && (timeout++ < 100));

    if (timeout >= 100) {
        return 2;
    }

    if (status & 0x1C) {
        return status;
    }

    if ((__inb(SMBHSTSTS) & 0x1F) != 0x00) {
        __outb(inb(SMBHSTSTS), SMBHSTSTS);
    }

    return 0;
}

static uint8_t nf_read_spd_byte(uint8_t smbus_adr, uint8_t spd_adr)
{
    int i;

    smbus_adr += 0x50;

    // Set Slave ADR
    __outb(smbus_adr << 1, NVSMBADD);

    // Set Command (SPD Byte to Read)
    __outb(spd_adr, NVSMBCMD);

    // Start transaction
    __outb(NVSMBCNT_BYTE_DATA | NVSMBCNT_READ, NVSMBCNT);

    // Wait until transaction complete
    for (i = 500; i > 0; i--) {
        usleep(50);
        if (__inb(NVSMBCNT) == 0) {
            break;
        }
    }

    // If timeout or Error Status, quit
    if (i == 0 || __inb(NVSMBSTS) & NVSMBSTS_STATUS) {
        return 0xFF;
    }

    return __inb(NVSMBDAT(0));
}

static uint8_t ali_m1563_read_spd_byte(uint8_t smbus_adr, uint8_t spd_adr)
{
    int i;

    smbus_adr += 0x50;

    // Reset Status Register
     __outb(0xFF, SMBHSTSTS);

    // Set Slave ADR
    __outb((smbus_adr << 1 | I2C_READ), SMBHSTADD);

    __outb((__inb(SMBHSTCNT) & ~ALI_SMBHSTCNT_SIZEMASK) | (ALI_SMBHSTCNT_BYTE_DATA << 3), SMBHSTCNT);

    // Set Command (SPD Byte to Read)
    __outb(spd_adr, SMBHSTCMD);

    // Start transaction
    __outb(__inb(SMBHSTCNT) | SMBHSTCNT_START, SMBHSTCNT);

    // Wait until transaction complete
    for (i = 500; i > 0; i--) {
        usleep(50);
        if (!(__inb(SMBHSTSTS) & SMBHSTSTS_HOST_BUSY)) {
            break;
        }
    }
    // If timeout or Error Status, exit
    if (i == 0 || __inb(SMBHSTSTS) & ALI_SMBHSTSTS_BAD) {
        return 0xFF;
    }

    return __inb(SMBHSTDAT0);
}

static uint8_t ali_m1543_read_spd_byte(uint8_t smbus_adr, uint8_t spd_adr)
{
    int i;

    smbus_adr += 0x50;

    // Reset Status Register
     __outb(0xFF, SMBHSTSTS);

    // Set Slave ADR
    __outb((smbus_adr << 1 | I2C_READ), ALI_OLD_SMBHSTADD);

    // Set Command (SPD Byte to Read)
    __outb(spd_adr, ALI_OLD_SMBHSTCMD);

    // Start transaction
    __outb(ALI_OLD_SMBHSTCNT_BYTE_DATA, ALI_OLD_SMBHSTCNT);
    __outb(0xFF, ALI_OLD_SMBHSTSTART);

    // Wait until transaction complete
    for (i = 500; i > 0; i--) {
        usleep(50);
        if (!(__inb(SMBHSTSTS) & ALI_OLD_SMBHSTSTS_BUSY)) {
            break;
        }
    }

    // If timeout or Error Status, exit
    if (i == 0 || __inb(SMBHSTSTS) & ALI_OLD_SMBHSTSTS_BAD) {
        return 0xFF;
    }

    return __inb(ALI_OLD_SMBHSTDAT0);
}
