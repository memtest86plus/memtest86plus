// SPDX-License-Identifier: GPL-2.0
#ifndef ERROR_H
#define ERROR_H
/**
 * \file
 *
 * Provides functions that can be called by the memory tests to report errors.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include "test.h"

/**
 * Error flags passed by the tests to data_error().
 */
#define ERR_BADRAM      0x01    // the address is reliable: feed the BadRAM/memmap/pages records
#define ERR_LOCATE      0x02    // attribute the error to a DIMM
#define ERR_NO_CHIP     0x04    // DIMM level only: the failing bits do not identify a chip

/**
 * The number of errors recorded during the current run.
 */
extern uint64_t error_count;

/**
 * The number of correctable ECC errors recorded during the current run.
 */
extern uint64_t error_count_cecc;

/**
 * Initialises the error records.
 */
void error_init(void);

/**
 * Adds an address error to the error reports.
 */
void addr_error(testword_t *addr1, testword_t *addr2, testword_t good, testword_t bad);

/**
 * Adds a data error to the error reports.
 */
void data_error(testword_t *addr, testword_t good, testword_t bad, uint8_t flags);

/**
 * Adds an ECC error to the error reports.
 * ECC Error details are stored in ecc_status
 */
void ecc_error();

#if REPORT_PARITY_ERRORS
/**
 * Adds a parity error to the error reports.
 */
void parity_error(void);
#endif

/**
 * Refreshes the error display after the error mode is changed.
 */
void error_update(void);

#endif // ERROR_H
