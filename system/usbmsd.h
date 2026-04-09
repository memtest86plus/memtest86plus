// SPDX-License-Identifier: GPL-2.0
#ifndef USBMSD_H
#define USBMSD_H
/**
 * \file
 *
 * Provides USB Mass Storage (Bulk-Only Transport) support for reading and
 * writing sectors on a USB drive.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usbhcd.h"

/**
 * Initialises the mass storage device by issuing TEST UNIT READY and
 * READ CAPACITY commands. Populates msd->block_count and msd->block_size.
 *
 * \returns true if the device is ready and capacity was read successfully.
 */
bool msd_init(usb_msd_t *msd);

/**
 * Reads one or more sectors from the mass storage device.
 *
 * \param msd    - the mass storage device context.
 * \param lba    - the starting logical block address.
 * \param count  - the number of sectors to read.
 * \param buffer - the destination buffer (must be at least count * block_size).
 *
 * \returns true if all sectors were read successfully.
 */
bool msd_read_sectors(usb_msd_t *msd, uint32_t lba, uint32_t count, void *buffer);

/**
 * Writes one or more sectors to the mass storage device.
 *
 * \param msd    - the mass storage device context.
 * \param lba    - the starting logical block address.
 * \param count  - the number of sectors to write.
 * \param buffer - the source buffer (must be at least count * block_size).
 *
 * \returns true if all sectors were written successfully.
 */
bool msd_write_sectors(usb_msd_t *msd, uint32_t lba, uint32_t count, const void *buffer);

#endif // USBMSD_H
