// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
#ifndef _SPD_H_
#define _SPD_H_

#define LINE_SPD        13
#define MAX_SPD_SLOT    8

/** Rounding factors for timing computation
 *
 *  These factors are used as a configurable CEIL() function
 *  to get the upper int from a float past a specific decimal point.
 */

#define DDR5_ROUNDING_FACTOR    30
#define ROUNDING_FACTOR         0.9f

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
} ram_info;

extern ram_info ram;

void read_sku(char *sku, uint8_t slot_idx, uint16_t offset, uint8_t max_len);

void parse_spd_rdram (spd_info *spdi, uint8_t slot_idx);
void parse_spd_sdram (spd_info *spdi, uint8_t slot_idx);
void parse_spd_ddr   (spd_info *spdi, uint8_t slot_idx);
void parse_spd_ddr2  (spd_info *spdi, uint8_t slot_idx);
void parse_spd_ddr3  (spd_info *spdi, uint8_t slot_idx);
void parse_spd_ddr4  (spd_info *spdi, uint8_t slot_idx);
void parse_spd_ddr5  (spd_info *spdi, uint8_t slot_idx);
void print_spdi(spd_info spdi, uint8_t lidx);
uint8_t get_spd(uint8_t slot_idx, uint16_t spd_adr);

#endif // SPD_H
