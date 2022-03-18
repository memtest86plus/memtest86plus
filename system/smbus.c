// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2022 Samuel Demeulemeester
//

#include "display.h"

#include "io.h"
#include "tsc.h"
#include "pci.h"
#include "unistd.h"

#include "smbus.h"
#include "jedec_id.h"

#define LINE_SPD        13
#define MAX_SPD_SLOT    8

int smbdev, smbfun;
unsigned short smbusbase;

static bool isPage1 = false;

static spd_info parse_spd_ddr3(uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr4(uint8_t smb_idx, uint8_t slot_idx);
static void print_spdi(spd_info spdi, uint8_t lidx);

static int find_smb_controller(void);

static uint8_t ich5_process(void);
static void ich5_get_smb(uint8_t idx);
static uint8_t ich5_read_spd_byte(uint8_t adr, uint16_t cmd);

static const struct pci_smbus_controller smbcontrollers[] = {
    {0x8086, 0x24C3, "82801DB (ICH4)",          ich5_get_smb, ich5_read_spd_byte, HAS_DDR | HAS_DDR2},
    {0x8086, 0x24D3, "82801E (ICH5)",           ich5_get_smb, ich5_read_spd_byte, HAS_DDR | HAS_DDR2},
    {0x8086, 0x25A4, "6300ESB",                 ich5_get_smb, ich5_read_spd_byte, HAS_DDR | HAS_DDR2},
    {0x8086, 0x266A, "82801F (ICH6)",           ich5_get_smb, ich5_read_spd_byte, HAS_DDR | HAS_DDR2},
    {0x8086, 0x269B, "6310ESB/6320ESB",         ich5_get_smb, ich5_read_spd_byte, HAS_DDR2},
    {0x8086, 0x27DA, "82801G (ICH7)",           ich5_get_smb, ich5_read_spd_byte, HAS_DDR2},
    {0x8086, 0x283E, "82801H (ICH8)",           ich5_get_smb, ich5_read_spd_byte, HAS_DDR2},
    {0x8086, 0x2930, "82801I (ICH9)",           ich5_get_smb, ich5_read_spd_byte, HAS_DDR2},
    {0x8086, 0x5032, "EP80579 (Tolapai)",       ich5_get_smb, ich5_read_spd_byte, HAS_DDR2},
    {0x8086, 0x3A30, "ICH10",                   ich5_get_smb, ich5_read_spd_byte, HAS_DDR2 | HAS_DDR3},
    {0x8086, 0x3A60, "ICH10",                   ich5_get_smb, ich5_read_spd_byte, HAS_DDR2 | HAS_DDR3},
    {0x8086, 0x3B30, "5/3400 Series (PCH)",     ich5_get_smb, ich5_read_spd_byte, HAS_DDR2 | HAS_DDR3},
    {0x8086, 0x1C22, "6 Series (PCH)",          ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x1D22, "Patsburg (PCH)",          ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x1D70, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x1D71, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x1D72, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x2330, "DH89xxCC (PCH)",          ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x1E22, "Panther Point (PCH)",     ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x8C22, "Lynx Point (PCH)",        ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x9C22, "Lynx Point-LP (PCH)",     ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x1F3C, "Avoton (SOC)",            ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x8D22, "Wellsburg (PCH)",         ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x8D7D, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x8D7E, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x8D7F, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x23B0, "Coleto Creek (PCH)",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x8CA2, "Wildcat Point (PCH)",     ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x9CA2, "Wildcat Point-LP (PCH)",  ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x0F12, "BayTrail (SOC)",          ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0x2292, "Braswell (SOC)",          ich5_get_smb, ich5_read_spd_byte, HAS_DDR3},
    {0x8086, 0xA123, "Sunrise Point-H (PCH) ",  ich5_get_smb, ich5_read_spd_byte, HAS_DDR3 | HAS_DDR4},
    {0x8086, 0x9D23, "Sunrise Point-LP (PCH)",  ich5_get_smb, ich5_read_spd_byte, HAS_DDR3 | HAS_DDR4},
    {0x8086, 0x19DF, "Denverton  (SOC)",        ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x1BC9, "Emmitsburg (PCH)",        ich5_get_smb, ich5_read_spd_byte, HAS_DDR4 | HAS_DDR5},
    {0x8086, 0xA1A3, "Lewisburg (PCH)",         ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0xA223, "Lewisburg Super (PCH)",   ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0xA2A3, "Kaby Lake (PCH-H)",       ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x31D4, "Gemini Lake (SOC)",       ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0xA323, "Cannon Lake-H (PCH)",     ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x9DA3, "Cannon Lake-LP (PCH)",    ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x18DF, "Cedar Fork (PCH)",        ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x34A3, "Ice Lake-LP (PCH)",       ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x38A3, "Ice Lake-N (PCH)",        ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x02A3, "Comet Lake (PCH)",        ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x06A3, "Comet Lake-H (PCH)",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x4B23, "Elkhart Lake (PCH)",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0xA0A3, "Tiger Lake-LP (PCH)",     ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x43A3, "Tiger Lake-H (PCH)",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x4DA3, "Jasper Lake (SOC)",       ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0xA3A3, "Comet Lake-V (PCH)",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR4},
    {0x8086, 0x7AA3, "Alder Lake-S (PCH)",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR4 | HAS_DDR5},
    {0x8086, 0x51A3, "Alder Lake-P (PCH)",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR4 | HAS_DDR5},
    {0x8086, 0x54A3, "Alder Lake-M (PCH)",      ich5_get_smb, ich5_read_spd_byte, HAS_DDR4 | HAS_DDR5},

     // AMD SMBUS
     {0, 0, "", NULL, NULL, 0}
};


void print_smbus_startup_info(void) {

    int8_t index;
    uint8_t spdidx = 0, spd_line_idx = 0;

    spd_info curspd;

    index = find_smb_controller();

    if (index == -1) {
        return;
    }

    smbcontrollers[index].get_adr(index);

    for (spdidx = 0; spdidx < MAX_SPD_SLOT; spdidx++) {

        if (get_spd(index, spdidx, 0) != 0xFF) {

            switch(get_spd(index, spdidx, 2))
            {
                default:
                    continue;
                case 0x0C: // DDR4
                    curspd = parse_spd_ddr4(index, spdidx);
                    break;
                case 0x0B: // DDR3
                    curspd = parse_spd_ddr3(index, spdidx);
                    break;
                case 0x08: // DDR2

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
                    spdi.module_size,
                    spdi.type,
                    spdi.freq);

    // Print ECC status
    if(spdi.hasECC) {
        curcol = prints(LINE_SPD+lidx, curcol, " ECC");
    }

    // Print Manufacturer from JEDEC106
    for (i = 0; i < JEP106_CNT ; i++) {

        if (spdi.jedec_code == jep106[i].jedec_code) {
            curcol = printf(LINE_SPD+lidx, curcol, " - %s ", jep106[i].name);
            break;
        }
    }

    // If not present in JEDEC106, display raw JEDEC ID
    if(i == JEP106_CNT) {
        curcol = printf(LINE_SPD+lidx, curcol, " - Unknown (0x%x) ", spdi.jedec_code);
    }

    // Print SKU
    for(i = 0; i < spdi.sku_len; i++) {
        printc(LINE_SPD+lidx, curcol++, spdi.sku[i]);
    }

    // Print Manufacturing date (only if valid)
    if(curcol <= 72 && spdi.fab_year > 1 && spdi.fab_year < 30 && spdi.fab_week < 55) {
        curcol = printf(LINE_SPD+lidx, curcol, " (W%i'%i)", spdi.fab_week, spdi.fab_year);
    }

    // Print XMP Status
    if(spdi.XMP > 0) {
        curcol = prints(LINE_SPD+lidx, curcol, " *XMP*");
    }
}

static spd_info parse_spd_ddr4(uint8_t smb_idx, uint8_t slot_idx)
{

    spd_info spdi;

    uint8_t tck, bcd;
    int8_t j;

    spdi.type = "DDR4";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;

    // Compute module size with shifts
    spdi.module_size = 1 << (
                             ((get_spd(smb_idx, slot_idx, 4) & 0xF) + 15)  +
                             ((get_spd(smb_idx, slot_idx, 13) & 0x7) + 3)  -
                             ((get_spd(smb_idx, slot_idx, 12) & 0x7) + 2)  +
                             ((get_spd(smb_idx, slot_idx, 12) >> 3) & 0x7) +
                             ((get_spd(smb_idx, slot_idx, 6) >> 4) & 0x7)
                            );

    spdi.hasECC = (((get_spd(smb_idx, slot_idx, 13) >> 3) & 1) == 1);

    tck = get_spd(smb_idx, slot_idx, 18);

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
    spdi.jedec_code= (get_spd(smb_idx, slot_idx, 320) & 0x1F) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, 321) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (j = 0; j <= 20; j++) {
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
    bcd = get_spd(smb_idx, slot_idx, 323);
    spdi.fab_year =  bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 324);
    spdi.fab_week =  bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr3(uint8_t smb_idx, uint8_t slot_idx)
{

    spd_info spdi;

    uint8_t tck, bcd;
    int8_t j;

    spdi.type = "DDR3";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.XMP = 0;

    // Compute module size with shifts
    spdi.module_size = 1 << (
                             ((get_spd(smb_idx, slot_idx, 4) & 0xF) + 15)  +
                             ((get_spd(smb_idx, slot_idx, 8) & 0x7) + 3)  -
                             ((get_spd(smb_idx, slot_idx, 7) & 0x7) + 2)  +
                             ((get_spd(smb_idx, slot_idx, 7) >> 3) & 0x7)
                            );

    spdi.hasECC = (((get_spd(smb_idx, slot_idx, 8) >> 3) & 1) == 1);

    tck = get_spd(smb_idx, slot_idx, 12);

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
    for (j = 0; j <= 20; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 128+j);

        if(sku_byte <= 0x20 && j > 0 && spdi.sku[j-1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    bcd = get_spd(smb_idx, slot_idx, 120);
    spdi.fab_year =  bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 121);
    spdi.fab_week =  bcd - 6 * (bcd >> 4);

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

static void ich5_get_smb(uint8_t idx)
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

    // Reset DDR4 Page because BIOS might pass control with Page1 set
    if(smbcontrollers[idx].cap & HAS_DDR4) {
        __outb((0x36 << 1) | 0x00, SMBHSTADD);
        __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

        ich5_process();
    }
}

static uint8_t ich5_read_spd_byte(uint8_t smbus_adr, uint16_t spd_adr)
{

    smbus_adr += 0x50;

    // Switch page if needed (DDR4)
    if (spd_adr > 0xFF && !isPage1) {

        __outb((0x37 << 1) | 0x00, SMBHSTADD);
        __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

        ich5_process(); // return should 0x42 or 0x44

        isPage1 = true;

    } else if (spd_adr <= 0xFF && isPage1) {

        __outb((0x36 << 1) | 0x00, SMBHSTADD);
        __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

        ich5_process();

        isPage1 = false;
    }

    if (spd_adr > 0xFF) {
        spd_adr -= 0x100;
    }

    __outb((smbus_adr << 1) | 0x01, SMBHSTADD);
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