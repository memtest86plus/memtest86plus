// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2022 Samuel Demeulemeester
//

#include "display.h"

#include "io.h"
#include "tsc.h"
#include "pci.h"
#include "unistd.h"

#include "cpuinfo.h"
#include "smbus.h"
#include "smbios.h"
#include "jedec_id.h"

#define LINE_SPD        13
#define MAX_SPD_SLOT    8

int smbdev, smbfun;
unsigned short smbusbase;

static int8_t spd_page = -1;
static int8_t last_adr = -1;

static spd_info parse_spd_ddr (uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr2(uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr3(uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr4(uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr5(uint8_t smb_idx, uint8_t slot_idx);
static void print_spdi(spd_info spdi, uint8_t lidx);

static int find_smb_controller(void);

static void fch_zen_get_smb(void);

static void ich5_get_smb(void);
static uint8_t ich5_process(void);
static uint8_t ich5_read_spd_byte(uint8_t adr, uint16_t cmd);

static const struct pci_smbus_controller smbcontrollers[] = {
    {0x8086, 0x24C3, "82801DB (ICH4)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x24D3, "82801E (ICH5)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x25A4, "6300ESB",                 ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x266A, "82801F (ICH6)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x269B, "6310ESB/6320ESB",         ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x27DA, "82801G (ICH7)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x283E, "82801H (ICH8)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2930, "82801I (ICH9)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x5032, "EP80579 (Tolapai)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x3A30, "ICH10",                   ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x3A60, "ICH10",                   ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x3B30, "5/3400 Series (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1C22, "6 Series (PCH)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1D22, "Patsburg (PCH)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1D70, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1D71, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1D72, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2330, "DH89xxCC (PCH)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1E22, "Panther Point (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8C22, "Lynx Point (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x9C22, "Lynx Point-LP (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1F3C, "Avoton (SOC)",            ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8D22, "Wellsburg (PCH)",         ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8D7D, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8D7E, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8D7F, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x23B0, "Coleto Creek (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8CA2, "Wildcat Point (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x9CA2, "Wildcat Point-LP (PCH)",  ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x0F12, "BayTrail (SOC)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2292, "Braswell (SOC)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA123, "Sunrise Point-H (PCH) ",  ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x9D23, "Sunrise Point-LP (PCH)",  ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x19DF, "Denverton  (SOC)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1BC9, "Emmitsburg (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA1A3, "Lewisburg (PCH)",         ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA223, "Lewisburg Super (PCH)",   ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA2A3, "Kaby Lake (PCH-H)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x31D4, "Gemini Lake (SOC)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA323, "Cannon Lake-H (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x9DA3, "Cannon Lake-LP (PCH)",    ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x18DF, "Cedar Fork (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x34A3, "Ice Lake-LP (PCH)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x38A3, "Ice Lake-N (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x02A3, "Comet Lake (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x06A3, "Comet Lake-H (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x4B23, "Elkhart Lake (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA0A3, "Tiger Lake-LP (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x43A3, "Tiger Lake-H (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x4DA3, "Jasper Lake (SOC)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA3A3, "Comet Lake-V (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x7AA3, "Alder Lake-S (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x51A3, "Alder Lake-P (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x54A3, "Alder Lake-M (PCH)",      ich5_get_smb, ich5_read_spd_byte},

     // AMD SMBUS
     {0x1022, 0x780B, "AMD FCH", NULL, NULL},
     {0x1022, 0x790B, "AMD FCH (Zen)",          fch_zen_get_smb, ich5_read_spd_byte},
     {0, 0, "", NULL, NULL}
};

void print_smbus_startup_info(void) {

    int8_t index;
    uint8_t spdidx = 0, spd_line_idx = 0;

    spd_info curspd;

    index = find_smb_controller();

    if (index == -1) {
        return;
    }

    smbcontrollers[index].get_adr();

    for (spdidx = 0; spdidx < MAX_SPD_SLOT; spdidx++) {

        if (get_spd(index, spdidx, 0) != 0xFF) {

            switch(get_spd(index, spdidx, 2))
            {
                default:
                    continue;
                case 0x12: // DDR5
                    curspd = parse_spd_ddr5(index, spdidx);
                    break;
                case 0x0C: // DDR4
                    curspd = parse_spd_ddr4(index, spdidx);
                    break;
                case 0x0B: // DDR3
                    curspd = parse_spd_ddr3(index, spdidx);
                    break;
                case 0x08: // DDR2
                    curspd = parse_spd_ddr2(index, spdidx);
                    break;
                case 0x07: // DDR
                    curspd = parse_spd_ddr(index, spdidx);
                    break;
            }

            if(curspd.isValid) {
                if(spd_line_idx == 0) {
                    prints(LINE_SPD-2, 0, "Memory SPD Informations");
                    prints(LINE_SPD-1, 0, "-----------------------");
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
    curcol = printf(LINE_SPD+lidx, 0, " - Slot %i : %kB %s-%i",
                    spdi.slot_num,
                    spdi.module_size * 1024,
                    spdi.type,
                    spdi.freq);

    // Print ECC status
    if(spdi.hasECC) {
        curcol = prints(LINE_SPD+lidx, curcol, " ECC");
    }

    // Print Manufacturer from JEDEC106
    for (i = 0; i < JEP106_CNT ; i++) {

        if (spdi.jedec_code == jep106[i].jedec_code) {
            curcol = printf(LINE_SPD+lidx, curcol, " - %s", jep106[i].name);
            break;
        }
    }

    // If not present in JEDEC106, display raw JEDEC ID
    if(i == JEP106_CNT && spdi.jedec_code != 0) {
        curcol = printf(LINE_SPD+lidx, curcol, " - Unknown (0x%x)", spdi.jedec_code);
    }

    // Print SKU
    for(i = 0; i < spdi.sku_len; i++) {
        printc(LINE_SPD+lidx, ++curcol, spdi.sku[i]);
    }

    // Print Manufacturing date (only if valid)
    if(curcol <= 72 && spdi.fab_year > 1 && spdi.fab_year < 32 && spdi.fab_week < 53) {
        curcol = printf(LINE_SPD+lidx, curcol, " (W%02i'%02i)", spdi.fab_week, spdi.fab_year);
    }

    // Print XMP Status
    if(spdi.XMP > 0) {
        curcol = prints(LINE_SPD+lidx, curcol, " *XMP*");
    }
}

static spd_info parse_spd_ddr5(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR5";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.module_size = 0;

    // Compute module size for symmetric & assymetric configuration
    for (int sbyte_adr = 1; sbyte_adr <= 2; sbyte_adr++) {
        uint32_t cur_rank = 0;
        uint8_t sbyte =  get_spd(smb_idx, slot_idx, sbyte_adr * 4);

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
        if((sbyte >> 5) > 1 && (sbyte >> 5) <= 5) {
            cur_rank *=  1 << (((sbyte >> 5) & 7) - 1);
        }

        sbyte = get_spd(smb_idx, slot_idx, 235);
        spdi.hasECC = (((sbyte >> 3) & 3) > 0);

        // Channels per DIMM
        if(((sbyte >> 5) & 3) == 1) {
            cur_rank *= 2;
        }

        // Primary Bus Width per Channel
        cur_rank *= 1 << ((sbyte & 3) + 3);

        // I/O Width
        sbyte = get_spd(smb_idx, slot_idx, (sbyte_adr * 4) + 2);
        cur_rank /= 1 << (((sbyte >> 5) & 3) + 2);

        // Add current rank to total package size
        spdi.module_size += cur_rank;

        // If not Asymmetrical, don't process the second rank
        if((get_spd(smb_idx, slot_idx, 234) >> 6) == 0) {
            break;
        }
    }

    // Compute Frequency (including XMP)
    uint16_t tCK;

    spdi.XMP = ((get_spd(smb_idx, slot_idx, 640) == 0x0C && get_spd(smb_idx, slot_idx, 641) == 0x4A)) ? 3 : 0;

    if(spdi.XMP == 3) {
        // XMP 3.0
        tCK = get_spd(smb_idx, slot_idx, 710) << 8;
        tCK |= get_spd(smb_idx, slot_idx, 709);
    } else {
        // JEDEC
        tCK = get_spd(smb_idx, slot_idx, 21) << 8;
        tCK |= get_spd(smb_idx, slot_idx, 20);
    }

    spdi.freq = (float)(1.0f / tCK * 2.0f * 1000.0f * 1000.0f);
    spdi.freq = (spdi.freq + 50) / 100 * 100;

    // Module manufacturer
    spdi.jedec_code = (get_spd(smb_idx, slot_idx, 512) & 0x1F) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, 513) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j <= 29; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 521+j);

        if(sku_byte <= 0x20 && j > 1 && spdi.sku[j-1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    // Week & Date (BCD to Int)
    uint8_t bcd = get_spd(smb_idx, slot_idx, 515);
    spdi.fab_year =  bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 516);
    spdi.fab_week =  bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr4(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR4";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;

    // Compute module size in MB with shifts
    spdi.module_size = 1 << (
                             ((get_spd(smb_idx, slot_idx, 4) & 0xF) + 5)  +
                             ((get_spd(smb_idx, slot_idx, 13) & 0x7) + 3)  -
                             ((get_spd(smb_idx, slot_idx, 12) & 0x7) + 2)  +
                             ((get_spd(smb_idx, slot_idx, 12) >> 3) & 0x7) +
                             ((get_spd(smb_idx, slot_idx, 6) >> 4) & 0x7)
                            );

    spdi.hasECC = (((get_spd(smb_idx, slot_idx, 13) >> 3) & 1) == 1);

    uint8_t tck = get_spd(smb_idx, slot_idx, 18);

    if(get_spd(smb_idx, slot_idx, 384) == 0x0C && get_spd(smb_idx, slot_idx, 385) == 0x4A) {
        // Max XMP
        uint8_t tck_mtb = get_spd(smb_idx, slot_idx, 396);
        int8_t tck_ftb = get_spd(smb_idx, slot_idx, 431);

        float tckavg =  1.0f / ((tck_mtb * 0.125f) + (tck_ftb * 0.001f)) * 2.0f * 1000.0f;

        spdi.freq = (tckavg+50)/100;
        spdi.freq *= 100;

        spdi.XMP = 2;

    } else {
        // Max JEDEC
        spdi.XMP = 0;

        switch(tck) {
            default:
                spdi.freq = 0;
                break;
            case 10:
                spdi.freq = 1600;
                break;
            case 9:
                spdi.freq = 1866;
                break;
            case 8:
                spdi.freq = 2133;
                break;
            case 7:
                spdi.freq = 2400;
                break;
            case 6:
                spdi.freq = 2666;
                break;
            case 5:
                spdi.freq = 3200;
                break;
        }
    }

    // Module manufacturer
    spdi.jedec_code = (get_spd(smb_idx, slot_idx, 320) & 0x1F) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, 321) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j <= 20; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 329+j);

        if(sku_byte <= 0x20 && j > 1 && spdi.sku[j-1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    // Week & Date (BCD to Int)
    uint8_t bcd = get_spd(smb_idx, slot_idx, 323);
    spdi.fab_year =  bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 324);
    spdi.fab_week =  bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr3(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR3";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.XMP = 0;

    // Compute module size in MB with shifts
    spdi.module_size = 1 << (
                             ((get_spd(smb_idx, slot_idx, 4) & 0xF) + 5)  +
                             ((get_spd(smb_idx, slot_idx, 8) & 0x7) + 3)  -
                             ((get_spd(smb_idx, slot_idx, 7) & 0x7) + 2)  +
                             ((get_spd(smb_idx, slot_idx, 7) >> 3) & 0x7)
                            );

    spdi.hasECC = (((get_spd(smb_idx, slot_idx, 8) >> 3) & 1) == 1);

    uint8_t tck = get_spd(smb_idx, slot_idx, 12);

    if(get_spd(smb_idx, slot_idx, 176) == 0x0C && get_spd(smb_idx, slot_idx, 177) == 0x4A) {
        tck = get_spd(smb_idx, slot_idx, 186);
        spdi.XMP = 1;
    }

    // Module jedec speed
    switch(tck) {
        default:
            spdi.freq = 0;
            break;
        case 20:
            spdi.freq = 800;
            break;
        case 15:
            spdi.freq = 1066;
            break;
        case 12:
            spdi.freq = 1333;
            break;
        case 10:
            spdi.freq = 1600;
            break;
        case 9:
            spdi.freq = 1866;
            break;
        case 8:
            spdi.freq = 2133;
            break;
        case 7:
            spdi.freq = 2400;
            break;
        case 6:
            spdi.freq = 2666;
            break;
    }

    // Module manufacturer
    spdi.jedec_code= (get_spd(smb_idx, slot_idx, 117) & 0x1F) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, 118) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j <= 20; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 128+j);

        if(sku_byte <= 0x20 && j > 0 && spdi.sku[j-1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    uint8_t bcd = get_spd(smb_idx, slot_idx, 120);
    spdi.fab_year =  bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 121);
    spdi.fab_week =  bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr2(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR2";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.XMP = 0;

    // Compute module size in MB
    switch (get_spd(smb_idx, slot_idx, 31)) {
        case 1:
            spdi.module_size = 1024;
            break;
        case 2:
            spdi.module_size = 2048;
            break;
        case 4:
            spdi.module_size = 4096;
            break;
        case 8:
            spdi.module_size = 8192;
            break;
        case 16:
            spdi.module_size = 16384;
            break;
        case 32:
            spdi.module_size = 128;
            break;
        case 64:
            spdi.module_size = 256;
            break;
        default:
        case 128:
            spdi.module_size = 512;
            break;
    }

    spdi.module_size *= (get_spd(smb_idx, slot_idx, 5) & 7) + 1;

    spdi.hasECC = ((get_spd(smb_idx, slot_idx, 11) >> 1) == 1);

    // Module speed
    uint8_t spd_byte9 = get_spd(smb_idx, slot_idx, 9);
    spdi.freq = (float)(1.0f / (((spd_byte9 >> 4) * 10.0f) + (spd_byte9 & 0xF)) * 10000.0f * 2.0f);

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(smb_idx, slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi.jedec_code = (contcode - 64) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, contcode) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j < 18; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 73 + j);

        if (sku_byte <= 0x20 && j > 0 && spdi.sku[j - 1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    uint8_t bcd = get_spd(smb_idx, slot_idx, 94);
    spdi.fab_year = bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 93);
    spdi.fab_week = bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.XMP = 0;

    // Compute module size in MB
    switch (get_spd(smb_idx, slot_idx, 31)) {
        case 1:
            spdi.module_size = 1024;
            break;
        case 2:
            spdi.module_size = 2048;
            break;
        case 4:
            spdi.module_size = 4096;
            break;
        case 8:
            spdi.module_size = 32;
            break;
        case 16:
            spdi.module_size = 64;
            break;
        case 32:
            spdi.module_size = 128;
            break;
        case 64:
            spdi.module_size = 256;
            break;
        case 128:
            spdi.module_size = 512;
            break;
        default: // we don't support asymetric banking
            spdi.module_size = 0;
            break;
    }

    spdi.module_size *= get_spd(smb_idx, slot_idx, 5);

    spdi.hasECC = ((get_spd(smb_idx, slot_idx, 11) >> 1) == 1);

    // Module speed
    uint8_t spd_byte9 = get_spd(smb_idx, slot_idx, 9);
    spdi.freq = (float)(1.0f / (((spd_byte9 >> 4) * 10.0f) + (spd_byte9 & 0xF)) * 10000.0f * 2.0f);

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(smb_idx, slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi.jedec_code = (contcode - 64) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, contcode) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j < 18; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 73 + j);

        if (sku_byte <= 0x20 && j > 0 && spdi.sku[j - 1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    uint8_t bcd = get_spd(smb_idx, slot_idx, 93);
    spdi.fab_year = bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 94);
    spdi.fab_week = bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

// --------------------------
// Smbus Controller Functions
// --------------------------

static int find_smb_controller(void)
{
    int i = 0;
    unsigned long valuev, valued;

    for (smbdev = 0; smbdev < 32; smbdev++) {
        for (smbfun = 0; smbfun < 8; smbfun++) {
            valuev = pci_config_read16(0, smbdev, smbfun, 0);
            if (valuev != 0xFFFF) {
                for (i = 0; smbcontrollers[i].vendor > 0; i++) {
                    if (valuev == smbcontrollers[i].vendor) {
                        valued = pci_config_read16(0, smbdev, smbfun, 2);
                        if (valued == smbcontrollers[i].device) {
                            return i;
                        }
                    }
                }
            }
        }
    }

    return -1;
}

// ------------------
// i801 / ICH5 Access
// ------------------

static void ich5_get_smb(void)
{
    unsigned long x;

    x = pci_config_read16(0, smbdev, smbfun, 0x20);
    smbusbase = (unsigned short) x & 0xFFF0;

    // Enable I2C Bus
    uint8_t temp = pci_config_read8(0, smbdev, smbfun, 0x40);
    if((temp & 4) == 0) {
        pci_config_write8(0, smbdev, smbfun, 0x40, temp | 0x04);
    }

    // Reset SMBUS Controller
    __outb(__inb(SMBHSTSTS) & 0x1F, SMBHSTSTS);
    usleep(1000);
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

// --------------------
// AMD SMBUS Controller
// --------------------

static void fch_zen_get_smb(void)
{
    uint16_t pm_reg;

    smbusbase = 0;

    __outb(AMD_PM_INDEX + 1, AMD_INDEX_IO_PORT);
    pm_reg = __inb(AMD_DATA_IO_PORT) << 8;
    __outb(AMD_PM_INDEX, AMD_INDEX_IO_PORT);
    pm_reg |= __inb(AMD_DATA_IO_PORT);

    // Special case for AMD Cezanne (get smb address in memory)
    if (imc_type == IMC_K19_CZN && pm_reg == 0xFFFF) {
        smbusbase = ((*(const uint32_t *)(0xFED80000 + 0x300) >> 8) & 0xFF) << 8;
        return;
    }

    // Check if IO Smbus is enabled.
    if((pm_reg & 0x10) == 0){
        return;
    }

    if((pm_reg & 0xFF00) != 0) {
        smbusbase = pm_reg & 0xFF00;
    }
}
