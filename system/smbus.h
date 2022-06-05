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

#define SPD5_MR11 11

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
#define AMD_SMBUS_BASE_REG  0x2C
#define AMD_PM_INDEX        0x00

/* nVidia-Specific constants */
#define NV_SMBUS_ADR_REG        0x20
#define NV_OLD_SMBUS_ADR_REG    0x50

#define NVSMBCNT    smbusbase + 0
#define NVSMBSTS    smbusbase + 1
#define NVSMBADD    smbusbase + 2
#define NVSMBCMD    smbusbase + 3
#define NVSMBDAT(x) (smbusbase + 4 + (x))

#define NVSMBCNT_WRITE      0x00
#define NVSMBCNT_READ       0x01
#define NVSMBCNT_QUICK      0x02
#define NVSMBCNT_BYTE       0x04
#define NVSMBCNT_BYTE_DATA  0x06
#define NVSMBCNT_WORD_DATA  0x08

#define NVSMBSTS_DONE       0x80
#define NVSMBSTS_ALRM       0x40
#define NVSMBSTS_RES        0x20
#define NVSMBSTS_STATUS     0x1f

struct pci_smbus_controller{
    unsigned vendor;
    unsigned device;
    void (*get_adr)(void);
};

typedef struct spd_infos {
    bool        isValid;
    uint8_t     slot_num;
    uint16_t    jedec_code;
    uint32_t    module_size;
    char        *type;
    uint8_t     sku[32];
    uint8_t     sku_len;
    uint8_t     XMP;
    uint16_t    freq;
    bool        hasECC;
    uint8_t     fab_year;
    uint8_t     fab_week;
    uint16_t    tCL;
    uint16_t    tRCD;
    uint16_t    tRP;
    uint16_t    tRAS;
    uint16_t    tRC;
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

/**
 * Print SMBUS Info
 */

void print_smbus_startup_info(void);

#endif // SMBUS_H