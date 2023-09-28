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
void data_error(testword_t *addr, testword_t good, testword_t bad, bool use_for_badram);

/**
 * Adds one or more data errors to the error reports, version for data types wider than 64 bits.
 */
void data_error_wide(testword_t *addr, testword_t * good, testword_t * bad, unsigned int width, bool use_for_badram);

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
