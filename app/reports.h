// SPDX-License-Identifier: GPL-2.0
#ifndef REPORTS_H
#define REPORTS_H
/**
 * \file
 *
 * Provides the ability to save test results to a FAT32-formatted USB drive
 * and to emit a machine-parseable test log over the serial port.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

#include "test.h"

/**
 * Searches for a USB mass storage device, mounts its FAT32 filesystem,
 * formats the current test results, and writes them to a file.
 * Displays status messages in the popup area during the operation.
 */
void save_results_to_usb(void);

/**
 * Emits the serial log start banner. All serial log functions are no-ops
 * unless the "log" boot option was given.
 */
void serial_log_start(void);

/**
 * Emits the system information block (DMI, IMC, SPD) and enables the
 * periodic log output. Call when the first real test run starts.
 */
void serial_log_run_start(void);

typedef enum { SLOG_PASS_START, SLOG_PASS_END, SLOG_TEST_START, SLOG_TEST_END } slog_event_t;

/**
 * Emits a pass/test lifecycle event.
 */
void serial_log_event(slog_event_t event);

/**
 * Emits a progress line at most every SERIAL_LOG_INTERVAL seconds.
 * Call once per test tick; also counts ticks for the progress percentages.
 */
void serial_log_tick(void);

/**
 * Emits an error detail line (at most one per test).
 */
void serial_log_error(const char *type, testword_t page, testword_t offset, testword_t expect, testword_t actual);

#endif // REPORTS_H
