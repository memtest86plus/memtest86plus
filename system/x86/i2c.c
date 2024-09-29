// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2024 Sam Demeulemeester

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
#include "spd.h"
#include "hwquirks.h"

#define MAX_SPD_SLOT    8

int smbbus, smbdev, smbfun;
unsigned short smbusbase = 0;
uint32_t smbus_id = 0;
static uint16_t extra_initial_sleep_for_smb_transaction = 0;

static int8_t spd_page = -1;
static int8_t last_adr = -1;

// Functions Prototypes
static bool setup_smb_controller(void);
static bool find_smb_controller(uint16_t vid, uint16_t did);

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

void print_spd_startup_info(void)
{
    uint8_t spdidx = 0, spd_line_idx = 0;

    spd_info curspd;

    if (quirk.type & QUIRK_TYPE_SMBUS) {
        quirk.process();
    }

    if (!setup_smb_controller() || smbusbase == 0) {
        return;
    }

    for (spdidx = 0; spdidx < MAX_SPD_SLOT; spdidx++) {
        parse_spd(&curspd, spdidx);

        if (!curspd.isValid)
            continue;

        if (spd_line_idx == 0) {
            prints(ROW_SPD-2, 0, "Memory SPD Information");
            prints(ROW_SPD-1, 0, "----------------------");
        }

        print_spdi(curspd, ROW_SPD+spd_line_idx);
        spd_line_idx++;
    }
}

// --------------------------
// SMBUS Controller Functions
// --------------------------

static bool setup_smb_controller(void)
{
    uint16_t vid, did;

    for(smbbus = 0; smbbus < 0xFF; smbbus += 0x80) {
        for (smbdev = 0; smbdev < 32; smbdev++) {
            for (smbfun = 0; smbfun < 8; smbfun++) {
                vid = pci_config_read16(smbbus, smbdev, smbfun, 0);
                if (vid != 0xFFFF) {
                    did = pci_config_read16(smbbus, smbdev, smbfun, 2);
                    if (did != 0xFFFF) {
                        if (find_smb_controller(vid, did)) {
                            return true;
                        }
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
    //0x7E22,  // Meteor Lake-P (SOC)
    0x7F23,  // Arrow Lake-S (PCH)
    //0xA822,  // Lunar Lake
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
    x = pci_config_read16(smbbus, smbdev, smbfun, 0x4);

    if (!(x & 1)) {
        pci_config_write16(smbbus, smbdev, smbfun, 0x4, x | 1);
    }

    // Read Base Address
    x = pci_config_read16(smbbus, smbdev, smbfun, 0x20);
    smbusbase = x & 0xFFF0;

    // Enable I2C Host Controller Interface if disabled
    // Use SMBUS Mode for DDR5 to allow bank switch using Proc Call
    uint8_t temp = pci_config_read8(smbbus, smbdev, smbfun, 0x40);
    if ((temp & 4) == 0 && dmi_memory_device->type != DMI_DDR5) {
       pci_config_write8(smbbus, smbdev, smbfun, 0x40, temp | 0x04);
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

    rev_id = pci_config_read8(smbbus, smbdev, smbfun, 0x08);

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

uint8_t get_spd(uint8_t slot_idx, uint16_t spd_adr)
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

            __outb((smbus_adr << 1) | I2C_READ, SMBHSTADD);
            __outb(SPD5_MR11 & 0x7F, SMBHSTCMD);
            __outb(adr_page & 7, SMBHSTDAT0);
            __outb(0, SMBHSTDAT1);
            __outb(SMBHSTCNT_PROC_CALL, SMBHSTCNT);

            ich5_process();

            // These dummy read are mandatory to finish a Proc Call
            __inb(SMBHSTDAT0);
            __inb(SMBHSTDAT1);

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
