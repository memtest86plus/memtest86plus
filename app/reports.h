// SPDX-License-Identifier: GPL-2.0
#ifndef REPORTS_H
#define REPORTS_H
/**
 * \file
 *
 * Provides the ability to save test results to a FAT32-formatted USB drive.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

/**
 * Searches for a USB mass storage device, mounts its FAT32 filesystem,
 * formats the current test results, and writes them to a file.
 * Displays status messages in the popup area during the operation.
 */
void save_results_to_usb(void);

#endif // REPORTS_H
