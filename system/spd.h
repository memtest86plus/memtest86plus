#ifndef _SPD_H_
#define _SPD_H_
/**
 * SPDX-License-Identifier: GPL-2.0
 *
 * \file
 *
 * Provides access to SPD parsing and printing functions.
 *
 * Copyright (C) 2004-2023 Sam Demeulemeester.
 */

#define SPD_SKU_LEN         32

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

extern ram_info_t ram;

void print_spdi(spd_info spdi, uint8_t lidx);
void parse_spd(spd_info *spdi, uint8_t slot_idx);

#endif // SPD_H
