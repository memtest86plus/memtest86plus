// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester.

#include <stdbool.h>
#include <stdint.h>

#include "string.h"

#include "fat32.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define FAT_FREE            0x00000000

#define DIR_ENTRY_SIZE      32
#define DIR_ATTR_ARCHIVE    0x20

// MBR partition type IDs.
#define MBR_TYPE_FAT16_SMALL    0x04
#define MBR_TYPE_FAT16          0x06
#define MBR_TYPE_NTFS_EXFAT     0x07
#define MBR_TYPE_FAT32          0x0B
#define MBR_TYPE_FAT32_LBA      0x0C
#define MBR_TYPE_FAT16_LBA      0x0E
#define MBR_TYPE_GPT_PROTECTIVE 0xEE

// GPT header signature at sector 1.
static const char gpt_signature[8] = "EFI PART";

// "Basic Data Partition" GUID: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 (mixed-endian)
static const uint8_t gpt_basic_data_guid[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static bool is_eoc(const fat32_fs_t *fs, uint32_t cluster)
{
    if (fs->fat_type == 16) return cluster >= 0xFFF8;
    return cluster >= 0x0FFFFFF8;
}

static uint32_t eoc_value(const fat32_fs_t *fs)
{
    if (fs->fat_type == 16) return 0xFFFF;
    return 0x0FFFFFFF;
}

static bool is_fat_partition(uint8_t type)
{
    switch (type) {
      case MBR_TYPE_FAT16_SMALL:
      case MBR_TYPE_FAT16:
      case MBR_TYPE_FAT16_LBA:
      case MBR_TYPE_FAT32:
      case MBR_TYPE_FAT32_LBA:
        return true;
      default:
        return false;
    }
}

static uint32_t cluster_to_lba(const fat32_fs_t *fs, uint32_t cluster)
{
    return fs->data_start_lba + (cluster - 2) * fs->sectors_per_cluster;
}

static bool read_sector(fat32_fs_t *fs, uint32_t lba)
{
    return msd_read_sectors(fs->msd, fs->partition_lba + lba, 1, fs->sector_buf);
}

static bool write_sector(fat32_fs_t *fs, uint32_t lba)
{
    return msd_write_sectors(fs->msd, fs->partition_lba + lba, 1, fs->sector_buf);
}

static uint32_t fat_read_entry(fat32_fs_t *fs, uint32_t cluster)
{
    if (fs->fat_type == 32) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs->fat_start_lba + fat_offset / fs->bytes_per_sector;
        uint32_t offset_in_sector = fat_offset % fs->bytes_per_sector;

        if (!read_sector(fs, fat_sector)) return 0x0FFFFFFF;

        uint32_t val;
        memcpy(&val, fs->sector_buf + offset_in_sector, 4);
        return val & 0x0FFFFFFF;
    }

    // FAT16.
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fs->fat_start_lba + fat_offset / fs->bytes_per_sector;
    uint32_t offset_in_sector = fat_offset % fs->bytes_per_sector;

    if (!read_sector(fs, fat_sector)) return 0xFFFF;

    uint16_t val;
    memcpy(&val, fs->sector_buf + offset_in_sector, 2);
    return val;
}

static bool fat_write_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value)
{
    if (fs->fat_type == 32) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector_off = fat_offset / fs->bytes_per_sector;
        uint32_t offset_in_sector = fat_offset % fs->bytes_per_sector;

        for (int fat = 0; fat < fs->num_fats; fat++) {
            uint32_t fat_sector = fs->fat_start_lba + fat * fs->sectors_per_fat + fat_sector_off;

            if (!read_sector(fs, fat_sector)) return false;

            // Preserve the upper 4 bits of the existing entry.
            uint32_t existing;
            memcpy(&existing, fs->sector_buf + offset_in_sector, 4);
            uint32_t merged = (existing & 0xF0000000) | (value & 0x0FFFFFFF);
            memcpy(fs->sector_buf + offset_in_sector, &merged, 4);

            if (!write_sector(fs, fat_sector)) return false;
        }
        return true;
    }

    // FAT16.
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector_off = fat_offset / fs->bytes_per_sector;
    uint32_t offset_in_sector = fat_offset % fs->bytes_per_sector;
    uint16_t val16 = (uint16_t)value;

    for (int fat = 0; fat < fs->num_fats; fat++) {
        uint32_t fat_sector = fs->fat_start_lba + fat * fs->sectors_per_fat + fat_sector_off;

        if (!read_sector(fs, fat_sector)) return false;
        memcpy(fs->sector_buf + offset_in_sector, &val16, 2);
        if (!write_sector(fs, fat_sector)) return false;
    }
    return true;
}

static uint32_t fat_alloc_cluster(fat32_fs_t *fs)
{
    // Linear scan from cluster 2 to find a free entry.
    for (uint32_t cluster = 2; cluster < fs->max_cluster; cluster++) {
        uint32_t entry = fat_read_entry(fs, cluster);
        if (entry == FAT_FREE) {
            if (!fat_write_entry(fs, cluster, eoc_value(fs))) return 0;
            return cluster;
        }
    }
    return 0; // Disk full.
}

// Scan the root directory for a free 32-byte entry.
// Returns the LBA and byte offset of the free entry using out parameters.
static bool find_free_dir_entry(fat32_fs_t *fs,
                                uint32_t *out_lba, uint32_t *out_offset)
{
    // FAT12/16: fixed root directory area.
    if (fs->fat_type != 32) {
        for (uint32_t s = 0; s < fs->root_dir_sectors; s++) {
            if (!read_sector(fs, fs->root_dir_lba + s)) return false;

            for (uint32_t off = 0; off + DIR_ENTRY_SIZE <= fs->bytes_per_sector; off += DIR_ENTRY_SIZE) {
                uint8_t first_byte = fs->sector_buf[off];
                if (first_byte == 0x00 || first_byte == 0xE5) {
                    *out_lba = fs->root_dir_lba + s;
                    *out_offset = off;
                    return true;
                }
            }
        }
        return false;
    }

    // FAT32: root directory is a cluster chain.
    uint32_t cluster = fs->root_cluster;

    while (cluster >= 2 && !is_eoc(fs, cluster)) {
        uint32_t lba = cluster_to_lba(fs, cluster);

        for (int s = 0; s < fs->sectors_per_cluster; s++) {
            if (!read_sector(fs, lba + s)) return false;

            for (uint32_t off = 0; off + DIR_ENTRY_SIZE <= fs->bytes_per_sector; off += DIR_ENTRY_SIZE) {
                uint8_t first_byte = fs->sector_buf[off];
                if (first_byte == 0x00 || first_byte == 0xE5) {
                    *out_lba = lba + s;
                    *out_offset = off;
                    return true;
                }
            }
        }

        cluster = fat_read_entry(fs, cluster);
    }

    return false; // No free entry found.
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------


// Try to parse the BPB from the sector already in buf.
// Returns true if it looks like a valid FAT16/32 BPB.
static bool parse_fat_bpb(fat32_fs_t *fs, const uint8_t *buf)
{
    // Validate boot signature.
    if (buf[510] != 0x55 || buf[511] != 0xAA) return false;

    // Parse common BPB fields.
    uint16_t tmp16;
    memcpy(&tmp16, buf + 11, 2);
    fs->bytes_per_sector = tmp16;
    fs->sectors_per_cluster = buf[13];
    memcpy(&tmp16, buf + 14, 2);
    fs->reserved_sectors = tmp16;
    fs->num_fats = buf[16];

    if (fs->bytes_per_sector == 0 || fs->sectors_per_cluster == 0) return false;
    if (fs->bytes_per_sector != fs->msd->block_size) return false;

    uint16_t root_entry_count;
    memcpy(&root_entry_count, buf + 17, 2);

    // Read both sectors_per_fat fields (FAT12/16 at offset 22, FAT32 at offset 36).
    uint16_t spf16;
    memcpy(&spf16, buf + 22, 2);
    uint32_t spf32;
    memcpy(&spf32, buf + 36, 4);

    if (spf16 != 0) {
        fs->sectors_per_fat = spf16;
    } else if (spf32 != 0) {
        fs->sectors_per_fat = spf32;
    } else {
        return false;
    }

    // Compute root directory size (non-zero only for FAT12/16).
    fs->root_dir_sectors = ((root_entry_count * DIR_ENTRY_SIZE) +
                            (fs->bytes_per_sector - 1)) / fs->bytes_per_sector;

    // Derive layout (works for all FAT types: root_dir_sectors is 0 for FAT32).
    fs->fat_start_lba  = fs->reserved_sectors;
    fs->root_dir_lba   = fs->fat_start_lba + (uint32_t)fs->num_fats * fs->sectors_per_fat;
    fs->data_start_lba = fs->root_dir_lba + fs->root_dir_sectors;

    // Compute total sectors and determine FAT type from cluster count.
    uint16_t total_sectors_16;
    memcpy(&total_sectors_16, buf + 19, 2);
    uint32_t total_sectors_32;
    memcpy(&total_sectors_32, buf + 32, 4);
    uint32_t total_sectors = total_sectors_16 ? total_sectors_16 : total_sectors_32;
    if (total_sectors == 0) return false;
    if (total_sectors <= fs->data_start_lba) return false;

    uint32_t data_sectors = total_sectors - fs->data_start_lba;
    uint32_t count_clusters = data_sectors / fs->sectors_per_cluster;

    if (count_clusters < 4085) {
        return false; // FAT12 not supported.
    } else if (count_clusters < 65525) {
        fs->fat_type = 16;
    } else {
        fs->fat_type = 32;
    }

    fs->max_cluster = count_clusters + 2;

    // FAT32: root directory is a cluster chain.
    if (fs->fat_type == 32) {
        if (root_entry_count != 0) return false;
        memcpy(&fs->root_cluster, buf + 44, 4);
    } else {
        if (root_entry_count == 0) return false;
        fs->root_cluster = 0;
    }

    return true;
}

bool fat32_mount(fat32_fs_t *fs, usb_msd_t *msd, uint8_t *buf)
{
    fs->msd = msd;
    fs->sector_buf = buf;
    fs->partition_lba = 0;

    // Read sector 0.
    if (!msd_read_sectors(msd, 0, 1, buf)) return false;

    // First, try to parse sector 0 directly as a FAT VBR (unpartitioned drive).
    if (parse_fat_bpb(fs, buf)) return true;

    // Not a valid FAT VBR. Check if it's an MBR with a partition table.
    if (buf[510] != 0x55 || buf[511] != 0xAA) return false;

    // Check for GPT: MBR partition type 0xEE (protective MBR) in the first entry.
    bool is_gpt = false;
    for (int i = 0; i < 4; i++) {
        if (buf[446 + i * 16 + 4] == MBR_TYPE_GPT_PROTECTIVE) {
            is_gpt = true;
            break;
        }
    }

    if (is_gpt) {
        // Read GPT header at sector 1.
        if (!msd_read_sectors(msd, 1, 1, buf)) return false;

        // Validate GPT signature "EFI PART".
        if (memcmp(buf, gpt_signature, 8) != 0) return false;

        // Parse GPT header fields.
        uint64_t entry_lba;
        uint32_t entry_count, entry_size;
        memcpy(&entry_lba,   buf + 72, 8);
        memcpy(&entry_count, buf + 80, 4);
        memcpy(&entry_size,  buf + 84, 4);
        if (entry_size < 128 || entry_count == 0) return false;

        // Scan partition entries for a Basic Data Partition with a FAT VBR.
        uint32_t entries_per_sector = msd->block_size / entry_size;
        if (entries_per_sector == 0) entries_per_sector = 1;

        for (uint32_t i = 0; i < entry_count; i++) {
            // Read the sector containing this entry.
            uint32_t sector = (uint32_t)entry_lba + i / entries_per_sector;
            if (i % entries_per_sector == 0) {
                if (!msd_read_sectors(msd, sector, 1, buf)) break;
            }

            uint8_t *ent = buf + (i % entries_per_sector) * entry_size;

            // Check for Basic Data Partition GUID.
            if (memcmp(ent, gpt_basic_data_guid, 16) != 0) continue;

            // Get starting LBA (little-endian uint64_t at offset 32).
            uint64_t start_lba;
            memcpy(&start_lba, ent + 32, 8);
            if (start_lba == 0 || start_lba > 0xFFFFFFFF) continue;

            fs->partition_lba = (uint32_t)start_lba;

            // Read the VBR and try to parse as FAT.
            if (!msd_read_sectors(msd, (uint32_t)start_lba, 1, buf)) continue;
            if (parse_fat_bpb(fs, buf)) return true;
        }
        return false;
    }

    // Standard MBR: scan the 4 partition entries (at offsets 446, 462, 478, 494).
    // Re-read sector 0 since buf may have been clobbered by GPT check above.
    if (!msd_read_sectors(msd, 0, 1, buf)) return false;

    for (int i = 0; i < 4; i++) {
        uint8_t *entry = buf + 446 + i * 16;
        uint8_t type = entry[4];

        if (!is_fat_partition(type)) continue;

        // Read the partition start LBA (little-endian uint32_t at offset 8).
        uint32_t part_lba;
        memcpy(&part_lba, entry + 8, 4);
        if (part_lba == 0) continue;

        fs->partition_lba = part_lba;

        // Read the VBR from the partition.
        if (!msd_read_sectors(msd, part_lba, 1, buf)) continue;

        if (parse_fat_bpb(fs, buf)) return true;
    }

    return false;
}

bool fat32_write_file(fat32_fs_t *fs, const char *name_8_3, const void *data, uint32_t size)
{
    if (size == 0) return false;

    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint32_t num_clusters = (size + cluster_size - 1) / cluster_size;

    // Allocate clusters and chain them.
    uint32_t first_cluster = 0;
    uint32_t prev_cluster = 0;

    for (uint32_t i = 0; i < num_clusters; i++) {
        uint32_t cluster = fat_alloc_cluster(fs);
        if (cluster == 0) return false;

        if (first_cluster == 0) {
            first_cluster = cluster;
        }

        // Chain to previous cluster.
        if (prev_cluster != 0) {
            if (!fat_write_entry(fs, prev_cluster, cluster)) return false;
        }
        prev_cluster = cluster;
    }

    // Write file data to the allocated clusters.
    const uint8_t *src = (const uint8_t *)data;
    uint32_t remaining = size;
    uint32_t cluster = first_cluster;

    while (remaining > 0 && cluster >= 2 && !is_eoc(fs, cluster)) {
        uint32_t lba = cluster_to_lba(fs, cluster);

        for (int s = 0; s < fs->sectors_per_cluster && remaining > 0; s++) {
            uint32_t to_write = remaining < fs->bytes_per_sector ? remaining : fs->bytes_per_sector;

            // If partial sector, clear the buffer first.
            if (to_write < fs->bytes_per_sector) {
                memset(fs->sector_buf, 0, fs->bytes_per_sector);
            }
            memcpy(fs->sector_buf, src, to_write);

            if (!write_sector(fs, lba + s)) return false;

            src += to_write;
            remaining -= to_write;
        }

        cluster = fat_read_entry(fs, cluster);
    }

    // Create directory entry in root directory.
    uint32_t entry_lba, entry_offset;
    if (!find_free_dir_entry(fs, &entry_lba, &entry_offset)) {
        return false;
    }

    // Read the sector containing the directory entry.
    if (!read_sector(fs, entry_lba)) return false;

    // Build the 32-byte directory entry.
    uint8_t *entry = fs->sector_buf + entry_offset;
    memset(entry, 0, DIR_ENTRY_SIZE);

    // Filename (8 bytes name + 3 bytes extension).
    memcpy(entry, name_8_3, 11);

    // Attributes.
    entry[11] = DIR_ATTR_ARCHIVE;

    // First cluster high word (bytes 20-21).
    entry[20] = (first_cluster >> 16) & 0xFF;
    entry[21] = (first_cluster >> 24) & 0xFF;

    // First cluster low word (bytes 26-27).
    entry[26] = first_cluster & 0xFF;
    entry[27] = (first_cluster >> 8) & 0xFF;

    // File size (bytes 28-31, little-endian).
    memcpy(entry + 28, &size, 4);

    // Write back the directory sector.
    if (!write_sector(fs, entry_lba)) return false;

    return true;
}

// Check a directory entry against the MT86P_XX.TXT pattern and mark used slots.
static void check_dir_entry(const uint8_t *entry, bool *used)
{
    if (memcmp(entry, "MT86P_", 6) == 0 && memcmp(entry + 8, "TXT", 3) == 0) {
        int tens = entry[6] - '0';
        int ones = entry[7] - '0';
        if (tens >= 0 && tens <= 9 && ones >= 0 && ones <= 9) {
            int num = tens * 10 + ones;
            if (num >= 1 && num <= 99) {
                used[num] = true;
            }
        }
    }
}

bool fat32_next_filename(fat32_fs_t *fs, char *name_out)
{
    // Scan root directory for existing MT86P_XX.TXT files.
    bool used[100];
    memset(used, 0, sizeof(used));

    if (fs->fat_type != 32) {
        // FAT12/16: fixed root directory area.
        for (uint32_t s = 0; s < fs->root_dir_sectors; s++) {
            if (!read_sector(fs, fs->root_dir_lba + s)) break;

            for (uint32_t off = 0; off + DIR_ENTRY_SIZE <= fs->bytes_per_sector; off += DIR_ENTRY_SIZE) {
                uint8_t *entry = fs->sector_buf + off;
                if (entry[0] == 0x00) goto scan_done;
                if (entry[0] == 0xE5) continue;
                check_dir_entry(entry, used);
            }
        }
    } else {
        // FAT32: root directory is a cluster chain.
        uint32_t cluster = fs->root_cluster;
        while (cluster >= 2 && !is_eoc(fs, cluster)) {
            uint32_t lba = cluster_to_lba(fs, cluster);

            for (int s = 0; s < fs->sectors_per_cluster; s++) {
                if (!read_sector(fs, lba + s)) break;

                for (uint32_t off = 0; off + DIR_ENTRY_SIZE <= fs->bytes_per_sector; off += DIR_ENTRY_SIZE) {
                    uint8_t *entry = fs->sector_buf + off;
                    if (entry[0] == 0x00) goto scan_done;
                    if (entry[0] == 0xE5) continue;
                    check_dir_entry(entry, used);
                }
            }
            cluster = fat_read_entry(fs, cluster);
        }
    }

scan_done:
    // Find the first unused number.
    for (int n = 1; n <= 99; n++) {
        if (!used[n]) {
            // Build 8.3 name: "MT86P_NNTXT" (11 chars, no dot).
            name_out[0] = 'M';
            name_out[1] = 'T';
            name_out[2] = '8';
            name_out[3] = '6';
            name_out[4] = 'P';
            name_out[5] = '_';
            name_out[6] = '0' + (n / 10);
            name_out[7] = '0' + (n % 10);
            name_out[8] = 'T';
            name_out[9] = 'X';
            name_out[10] = 'T';
            name_out[11] = '\0';
            return true;
        }
    }
    return false; // All 99 slots used.
}
