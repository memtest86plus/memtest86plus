// SPDX-License-Identifier: GPL-2.0
#ifndef BOOTPARAMS_H
#define BOOTPARAMS_H
/**
 * \file
 *
 * Provides definitions for the boot params structure passed to us by
 * intermediate bootloaders when using the Linux boot protocol. This matches
 * the Linux boot_params struct, although we only define the fields we are
 * interested in.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdint.h>

typedef struct {
    uint8_t 	orig_x;
    uint8_t  	orig_y;
    uint16_t 	ext_mem_k;
    uint16_t 	orig_video_page;
    uint8_t  	orig_video_mode;
    uint8_t  	orig_video_cols;
    uint8_t  	flags;
    uint8_t  	unused2;
    uint16_t 	orig_video_ega_bx;
    uint16_t 	unused3;
    uint8_t  	orig_video_lines;
    uint8_t  	orig_video_isVGA;
    uint16_t 	orig_video_points;

    uint16_t 	lfb_width;
    uint16_t 	lfb_height;
    uint16_t 	lfb_depth;
    uint32_t 	lfb_base;
    uint32_t 	lfb_size;
    uint16_t 	cl_magic, cl_offset;
    uint16_t 	lfb_linelength;
    uint8_t  	red_size;
    uint8_t  	red_pos;
    uint8_t  	green_size;
    uint8_t  	green_pos;
    uint8_t  	blue_size;
    uint8_t  	blue_pos;
    uint8_t  	rsvd_size;
    uint8_t  	rsvd_pos;
    uint16_t 	vesapm_seg;
    uint16_t 	vesapm_off;
    uint16_t 	pages;
    uint16_t 	vesa_attributes;
    uint32_t 	capabilities;
    uint32_t 	ext_lfb_base;
    uint8_t  	_reserved[2];
} __attribute__((packed)) screen_info_t;

#define VIDEO_TYPE_VLFB             0x23    // VESA VGA in graphic mode
#define VIDEO_TYPE_EFI              0x70    // EFI graphic mode

#define LFB_CAPABILITY_64BIT_BASE   (1 << 1)

typedef struct {
    uint32_t    loader_signature;
    uint32_t    sys_tab;
    uint32_t    mem_desc_size;
    uint32_t    mem_desc_version;
    uint32_t    mem_map;
    uint32_t    mem_map_size;
    uint32_t    sys_tab_hi;
    uint32_t    mem_map_hi;
} __attribute__((packed)) efi_info_t;

#define EFI32_LOADER_SIGNATURE  ('E' | ('L' << 8) | ('3' << 16) | ('2' << 24))
#define EFI64_LOADER_SIGNATURE  ('E' | ('L' << 8) | ('6' << 16) | ('4' << 24))

typedef enum {
    E820_NONE       = 0,
    E820_RAM        = 1,
    E820_RESERVED   = 2,
    E820_ACPI       = 3,    // usable as RAM once ACPI tables have been read
    E820_NVS        = 4
} e820_type_t;

typedef struct {
    uint64_t        addr;
    uint64_t        size;
    uint32_t        type;
} __attribute__((packed)) e820_entry_t;

typedef struct {
    screen_info_t   screen_info;
    uint8_t         unused1[0x070 - 0x040];
    uint64_t        acpi_rsdp_addr;
    uint8_t         unused2[0x1c0 - 0x078];
    efi_info_t      efi_info;
    uint8_t         unused3[0x1e8 - 0x1e0];
    uint8_t         e820_entries;
    uint8_t         unused4[0x214 - 0x1e9];
    uint32_t        code32_start;
    uint8_t         unused5[0x228 - 0x218];
    uint32_t        cmd_line_ptr;
    uint8_t         unused6[0x238 - 0x22c];
    uint32_t        cmd_line_size;
    uint8_t         unused7[0x2d0 - 0x23c];
    e820_entry_t    e820_map[E820_MAP_SIZE];
    uint8_t         unused8[0xeec - 0xd00];
} __attribute__((packed)) boot_params_t;

#endif /* BOOTPARAMS_H */
