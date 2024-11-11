#ifndef _SMBUS_H_
#define _SMBUS_H_
/**
 * SPDX-License-Identifier: GPL-2.0
 *
 * \file
 *
 * Provides functions for reading SPD via SMBUS
 *
 * Copyright (C) 2004-2023 Sam Demeulemeester.
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

/* i801 Hosts Control register bits */
#define SMBHSTCNT_QUICK             0x00
#define SMBHSTCNT_BYTE              0x04
#define SMBHSTCNT_BYTE_DATA         0x08
#define SMBHSTCNT_WORD_DATA         0x0C
#define SMBHSTCNT_PROC_CALL         0x10
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

/* ALi-Specific constants (M1563 & newer) */
#define ALI_SMBHSTCNT_SIZEMASK  0x03
#define ALI_SMBHSTSTS_BAD       0x1C

#define ALI_SMBHSTCNT_QUICK     0x00
#define ALI_SMBHSTCNT_BYTE      0x01
#define ALI_SMBHSTCNT_BYTE_DATA 0x02
#define ALI_SMBHSTCNT_WORD_DATA 0x03
#define ALI_SMBHSTCNT_KILL      0x04
#define ALI_SMBHSTCNT_BLOCK     0x05

/* ALi-Specific constants (M1543 & older) */
#define ALI_OLD_SMBHSTSTS_BAD       0xE0
#define ALI_OLD_SMBHSTSTS_BUSY      0x08
#define ALI_OLD_SMBHSTCNT_BYTE_DATA 0x20

#define ALI_OLD_SMBHSTCNT   smbusbase + 1
#define ALI_OLD_SMBHSTSTART smbusbase + 2
#define ALI_OLD_SMBHSTADD   smbusbase + 3
#define ALI_OLD_SMBHSTDAT0  smbusbase + 4
#define ALI_OLD_SMBHSTCMD   smbusbase + 7

#define PIIX4_SMB_BASE_ADR_DEFAULT  0x90
#define PIIX4_SMB_BASE_ADR_VIAPRO   0xD0
#define PIIX4_SMB_BASE_ADR_ALI1563  0x80
#define PIIX4_SMB_BASE_ADR_ALI1543  0x14

struct pci_smbus_controller {
    unsigned vendor;
    unsigned device;
    void (*get_adr)(void);
};

/**
 * Print SPD Info
 */

void print_spd_startup_info(void);

uint8_t get_spd(uint8_t slot_idx, uint16_t spd_adr);

#endif // SMBUS_H
