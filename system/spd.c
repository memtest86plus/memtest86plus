// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester

#include "stdbool.h"
#include "stdint.h"
#include "string.h"

#include "smbus.h"
#include "spd.h"
#include "jedec_id.h"
#include "print.h"

/** Rounding factors for timing computation
 *
 *  These factors are used as a configurable CEIL() function
 *  to get the upper int from a float past a specific decimal point.
 */

#define DDR5_ROUNDING_FACTOR    30
#define ROUNDING_FACTOR         0.9f

ram_info_t ram = { 0, 0, 0, 0, 0, 0, "N/A"};

static inline uint8_t bcd_to_ui8(uint8_t bcd)
{
    return bcd - 6 * (bcd >> 4);
}

void print_spdi(spd_info spdi, uint8_t row)
{
    uint8_t curcol;
    uint16_t i;

    // Print Slot Index, Module Size, type & Max frequency (Jedec or XMP)
    curcol = printf(row, 0, " - Slot %i: %kB %s-%i",
                    spdi.slot_num,
                    spdi.module_size * 1024,
                    spdi.type,
                    spdi.freq);

    // Print ECC status
    if (spdi.hasECC) {
        curcol = prints(row, ++curcol, "ECC");
    }

    // Print XMP/EPP Status
    if (spdi.XMP > 0 && spdi.XMP < 20) {
        curcol = prints(row, ++curcol, "XMP");
    } else if (spdi.XMP == 20) {
        curcol = prints(row, ++curcol, "EPP");
    }

    // Print Manufacturer from JEDEC106
    for (i = 0; i < JEP106_CNT; i++) {
        if (spdi.jedec_code == jep106[i].jedec_code) {
            curcol = printf(row, ++curcol, "- %s", JEP106_NAME(i));
            break;
        }
    }

    // If not present in JEDEC106, display raw JEDEC ID
    if (spdi.jedec_code == 0) {
        curcol = prints(row, ++curcol, "- Noname");
    } else if (i == JEP106_CNT) {
        curcol = printf(row, ++curcol, "- Unknown (0x%x)", spdi.jedec_code);
    }

    // Print SKU
    if (*spdi.sku)
        curcol = prints(row, curcol + 1, spdi.sku);

    // Check manufacturing date and print if valid.
    // fab_year is uint8_t and carries only the last two digits.
    //  - for 0..31 we assume 20xx, and 0 means 2000.
    //  - for 96..99 we assume 19xx.
    //  - values 32..95 and > 99 are considered invalid.
    if (curcol <= 69 && spdi.fab_week <= 53 && spdi.fab_week != 0 &&
        (spdi.fab_year < 32 || (spdi.fab_year >= 96 && spdi.fab_year <= 99))) {
        curcol = printf(row, ++curcol, "(%02i%02i-W%02i)",
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

void parse_spd(spd_info *spdi, uint8_t slot_idx)
{
    memset(spdi, 0, sizeof(*spdi));     // Also sets isValid to False
    spdi->slot_num = slot_idx;

    if (get_spd(slot_idx, 0) == 0xFF)
        return;

    switch(get_spd(slot_idx, 2))
    {
        case 0x12: // DDR5
            parse_spd_ddr5(spdi, slot_idx);
            break;
        case 0x0C: // DDR4
            parse_spd_ddr4(spdi, slot_idx);
            break;
        case 0x0B: // DDR3
            parse_spd_ddr3(spdi, slot_idx);
            break;
        case 0x08: // DDR2
            parse_spd_ddr2(spdi, slot_idx);
            break;
        case 0x07: // DDR
            parse_spd_ddr(spdi, slot_idx);
            break;
        case 0x04: // SDRAM
            parse_spd_sdram(spdi, slot_idx);
            break;
        case 0x01: // RAMBUS - RDRAM
            if (get_spd(slot_idx, 1) == 8) {
                parse_spd_rdram(spdi, slot_idx);
            }
            break;
    }
}
