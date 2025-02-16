#ifndef _SPD_H_
#define _SPD_H_
/**
 * SPDX-License-Identifier: GPL-2.0
 *
 * \file
 *
 * Provides access to SPD parsing and printing functions.
 *
 * Copyright (C) 2004-2025 Sam Demeulemeester.
 */

#define MAX_SPD_SLOT    8
#define SPD_SKU_LEN     32

/* DDR5 SPD Hub Configuration Registers */

#define SPD5_HUB_ID_MSB     0x00    // MR0 - Device Type MSB
#define SPD5_HUB_ID_LSB     0x01    // MR1 - Device Type LSB (0x18 = w/ Temp Sensor)
#define SPD5_HUB_CAP        0x05    // MR5 - Device Capability (Bit 1 = Temp Sensor Support)
#define SPD5_HUB_I2C_CONF   0x0B    // MR11 - I²C Legacy Mode Device Configuration
#define SPD5_HUB_CONF       0x12    // MR18 - General Device Configuration
#define SPD5_HUB_TS_CONF    0x1A    // MR26 - Temperature Sensor Configuration (Bit 0 = 1 for Disable)
#define SPD5_HUB_TS_RES     0x24    // MR36 - TS Resolution (from 9- to 12-bit)
#define SPD5_HUB_STATUS     0x30    // MR48 - Device Status
#define SPD5_HUB_TS_LSB     0x31    // MR49 - TS Current Sensed Temperature - Low Byte
#define SPD5_HUB_TS_MSB     0x32    // MR50 - TS Current Sensed Temperature - High Byte
#define SPD5_HUB_TS_STATUS  0x33    // MR51 - Temperature Sensor Status

typedef struct spd_infos {
    bool        isValid;
    uint8_t     slot_num;
    uint16_t    jedec_code;
    uint32_t    module_size;
    char        *type;
    char        sku[SPD_SKU_LEN + 1];
    uint8_t     XMP;
    uint16_t    freq;
    bool        hasECC;
    bool        hasTempSensor;
    uint8_t     fab_year;
    uint8_t     fab_week;
    uint16_t    tCL;
    uint8_t     tCL_dec;
    uint16_t    tRCD;
    uint16_t    tRP;
    uint16_t    tRAS;
    uint16_t    tRC;
} spd_info;

typedef struct ram_infos {
    uint16_t    freq;
    uint16_t    tCL;
    uint8_t     tCL_dec;
    uint16_t    tRCD;
    uint16_t    tRP;
    uint16_t    tRAS;
    char        *type;
} ram_info_t;

typedef struct {
    uint8_t     slot_idx;
    uint8_t     display_idx;
    bool        isPopulated;
    bool        hasTempSensor;
} ram_slot_info_t;

extern ram_info_t ram;
extern ram_slot_info_t ram_slot_info[MAX_SPD_SLOT];

void print_spdi(spd_info spdi, uint8_t lidx);
void parse_spd(spd_info *spdi, uint8_t slot_idx);

#endif // SPD_H
