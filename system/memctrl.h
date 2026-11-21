// SPDX-License-Identifier: GPL-2.0
#ifndef MEMCTRL_H
#define MEMCTRL_H
/**
 * \file
 *
 * Provides information about the memory controller status
 * (running DRAM configuration, ECC, ...) and other
 * platform-specific data
 *
 *//*
 * Copyright (C) 2004-2023 Sam Demeulemeester.
 */

typedef struct __attribute__((packed)) imc_infos {
    char        *type;
    uint16_t    family;
    uint16_t    freq;
    uint16_t    width;
    uint16_t    tCL;
    uint8_t     tCL_dec;
    uint16_t    tRCD;
    uint16_t    tRP;
    uint16_t    tRAS;
} imc_info_t;

typedef enum {
    ECC_ERR_NONE,
    ECC_ERR_CORRECTED,
    ECC_ERR_UNCORRECTED,
    ERR_UNKNOWN
} ecc_error_type_t;

typedef struct __attribute__((packed)) ecc_status {
    bool                ecc_enabled;
    ecc_error_type_t    type;
    uint64_t            addr;
    uint32_t            count;
    uint16_t            core;
    uint8_t             channel;
} ecc_info_t;

/**
 * Location of a physical address in the DRAM topology (DIMM decoder).
 */

typedef struct {
    uint8_t     mc;         // memory controller
    uint8_t     ch;         // channel within MC
    uint8_t     dimm;       // DIMM within channel
    uint8_t     slot;       // spd_slot_cache[] index
} dram_loc_t;

/**
 * Current DRAM configuration of the Integrated Memory Controller
 */

extern imc_info_t imc;

/**
 * Current ECC Status of the Integrated Memory Controller
 */

extern ecc_info_t ecc_status;

void memctrl_init(void);

void memctrl_poll_ecc(void);

/**
 * Builds the DIMM slot topology and runs the decoder cross-checks (IMC vs
 * SPD). Call after the SPD data has been read.
 */
void memctrl_decode_init(void);

/**
 * Decodes a physical address to its DIMM location. Returns false if the
 * decoder is off or the address cannot be attributed. Pure computation,
 * callable from any CPU.
 */
bool memctrl_decode_addr(uint64_t pa, dram_loc_t *loc);

/**
 * Returns a short decoder status, e.g. "unsupported" or "off: IMC/SPD mismatch".
 */
const char *memctrl_decode_status(void);

#endif // MEMCTRL_H
