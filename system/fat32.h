// SPDX-License-Identifier: GPL-2.0
#ifndef FAT32_H
#define FAT32_H
/**
 * \file
 *
 * Provides a minimal write-only FAT16/32 filesystem for creating
 * files in the root directory of a FAT-formatted USB drive.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usbmsd.h"

/**
 * FAT16/32 filesystem context.
 */
typedef struct {
    usb_msd_t   *msd;
    uint32_t    partition_lba;      // LBA offset of the partition (0 if unpartitioned)
    uint32_t    bytes_per_sector;
    uint8_t     sectors_per_cluster;
    uint16_t    reserved_sectors;
    uint8_t     num_fats;
    uint8_t     fat_type;           // 16 or 32
    uint32_t    sectors_per_fat;
    uint32_t    root_cluster;       // FAT32: first cluster of root directory
    uint32_t    root_dir_lba;       // FAT12/16: start LBA of fixed root directory
    uint16_t    root_dir_sectors;   // FAT12/16: sector count of fixed root directory
    uint32_t    max_cluster;        // highest valid cluster number (exclusive)
    uint32_t    fat_start_lba;
    uint32_t    data_start_lba;
    uint8_t     *sector_buf;        // one sector buffer
} fat32_fs_t;

/**
 * Mounts a FAT16/32 filesystem by reading the BPB from sector 0.
 *
 * \param fs  - the filesystem context to populate.
 * \param msd - the mass storage device to read from.
 * \param buf - a sector buffer (must be at least msd->block_size bytes).
 *
 * \returns true if a valid FAT filesystem was found.
 */
bool fat32_mount(fat32_fs_t *fs, usb_msd_t *msd, uint8_t *buf);

/**
 * Creates a new file in the root directory and writes its contents.
 * The filename must be in 8.3 format padded with spaces (11 bytes).
 *
 * \param fs       - the mounted filesystem context.
 * \param name_8_3 - the filename in 8.3 format (exactly 11 characters, no dot).
 * \param data     - the file contents.
 * \param size     - the size of the file in bytes.
 *
 * \returns true if the file was created successfully.
 */
bool fat32_write_file(fat32_fs_t *fs, const char *name_8_3, const void *data, uint32_t size);

/**
 * Generates the next available filename in the format "MT86P_NN.TXT"
 * by scanning the root directory for existing files.
 *
 * \param fs       - the mounted filesystem context.
 * \param name_out - output buffer for the 8.3 name (at least 12 bytes).
 *
 * \returns true if a name was generated (false if all 99 slots are used).
 */
bool fat32_next_filename(fat32_fs_t *fs, char *name_out);

#endif // FAT32_H
