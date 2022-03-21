#ifndef _SMBUS_H_
#define _SMBUS_H_
/**
 * SPDX-License-Identifier: GPL-2.0
 *
 * \file
 *
 * Provides functions for reading SPD via SMBUS
 *
 * Copyright (C) 2004-2022 Samuel Demeulemeester.
 */

#define I2C_WRITE   0
#define I2C_READ    1

/* i801 Hosts Addresses */
#define SMBHSTSTS   smbusbase
#define SMBHSTCNT   smbusbase + 2
#define SMBHSTCMD   smbusbase + 3
#define SMBHSTADD   smbusbase + 4
#define SMBHSTDAT0  smbusbase + 5
#define SMBHSTDAT1  smbusbase + 6
#define SMBBLKDAT   smbusbase + 7
#define SMBPEC      smbusbase + 8
#define SMBAUXSTS   smbusbase + 12
#define SMBAUXCTL   smbusbase + 13

/* i801 Hosts Status register bits */
#define SMBHSTSTS_BYTE_DONE     0x80
#define SMBHSTSTS_INUSE_STS     0x40
#define SMBHSTSTS_SMBALERT_STS  0x20
#define SMBHSTSTS_FAILED        0x10
#define SMBHSTSTS_BUS_ERR       0x08
#define SMBHSTSTS_DEV_ERR       0x04
#define SMBHSTSTS_INTR          0x02
#define SMBHSTSTS_HOST_BUSY     0x01

#define SMBHSTCNT_QUICK             0x00
#define SMBHSTCNT_BYTE              0x04
#define SMBHSTCNT_BYTE_DATA         0x08
#define SMBHSTCNT_WORD_DATA         0x0C
#define SMBHSTCNT_BLOCK_DATA        0x14
#define SMBHSTCNT_I2C_BLOCK_DATA    0x18
#define SMBHSTCNT_LAST_BYTE         0x20
#define SMBHSTCNT_START             0x40

/* AMD-Specific constants */
#define AMD_INDEX_IO_PORT   0xCD6
#define AMD_DATA_IO_PORT    0xCD7
#define AMD_PM_INDEX        0x00

#define SPD5_MR11 11

struct pci_smbus_controller_ops {
    void (*get_adr)(void);
    uint8_t (*read_spd_byte)(uint8_t dimmadr, uint16_t bytenum);
};

struct pci_smbus_controller {
    uint16_t vendor;
    uint16_t device;
    uint16_t ops;
    char     *name;
};

typedef struct spd_infos {
    bool        isValid;
    uint32_t    module_size;
    uint8_t     slot_num;
    uint16_t    jedec_code;
    char        sku[32];
    uint8_t     sku_len;
    uint16_t    freq;
    uint8_t     XMP;
    bool        hasECC;
    uint8_t     fab_year;
    uint8_t     fab_week;
    uint16_t    tCL;
    uint16_t    tRCD;
    uint16_t    tRP;
    uint16_t    tRAS;
    uint16_t    tRC;
    char        *type;
} spd_info;

typedef struct ram_infos {
    uint16_t    freq;
    uint16_t    tCL;
    uint16_t    tRCD;
    uint16_t    tRP;
    uint16_t    tRAS;
    char        *type;
} ram_info;

extern ram_info ram;

#define get_spd(smb_idx, slot_idx, spd_adr) \
    smbcontrollerops[smbcontrollers[smb_idx].ops].read_spd_byte(slot_idx, spd_adr)

/**
 * Print SMBUS Info
 */

void print_smbus_startup_info(void);

#endif // SMBUS_H