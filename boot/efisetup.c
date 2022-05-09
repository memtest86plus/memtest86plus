// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from Linux 5.6 arch/x86/boot/compressed/eboot.c and extracts
// from drivers/firmware/efi/libstub:
//
//   Copyright 2011 Intel Corporation; author Matt Fleming

#include <stdbool.h>

#include "boot.h"
#include "bootparams.h"
#include "efi.h"

#include "memsize.h"

#include "string.h"

#define DEBUG 0

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAP_BUFFER_HEADROOM     8       // number of descriptors

#define MIN_H_RESOLUTION        640     // as required by our main display
#define MIN_V_RESOLUTION        400

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static efi_guid_t EFI_CONSOLE_OUT_DEVICE_GUID         = { 0xd3b36f2c, 0xd551, 0x11d4, {0x9a, 0x46, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };
static efi_guid_t EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID   = { 0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a} };
static efi_guid_t EFI_LOADED_IMAGE_PROTOCOL_GUID      = { 0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b} };

static efi_system_table_t *sys_table = NULL;

//------------------------------------------------------------------------------
// Macro Functions
//------------------------------------------------------------------------------

#define round_up(value, align) \
    (((value) + (align) - 1) & ~((align) - 1))

// The following macros are used in Linux to hide differences in mixed mode.
// For now, just support native mode.

#define efi_table_attr(table, attr) \
    table->attr

#define efi_call_proto(proto, func, ...) \
    proto->func(proto, ##__VA_ARGS__)

#define efi_call_bs(func, ...) \
    sys_table->boot_services->func(__VA_ARGS__)

#define efi_call_rs(func, ...) \
    sys_table->runtime_services->func(__VA_ARGS__)

#define efi_get_num_handles(size) \
    (int)((size) / sizeof(efi_handle_t))

#define efi_get_handle_at(array, index) \
    (array)[index]

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void print_unicode_string(efi_char16_t *str)
{
    efi_call_proto(efi_table_attr(sys_table, con_out), output_string, str);
}

static void print_string(char *str)
{
    char *s8;

    for (s8 = str; *s8; s8++) {
        efi_char16_t ch[2] = { 0 };

        ch[0] = *s8;
        if (*s8 == '\n') {
            efi_char16_t cr[2] = { '\r', 0 };
            print_unicode_string(cr);
        }

        print_unicode_string(ch);
    }
}

#if DEBUG
static void print_dec(unsigned value)
{
    char buffer[16];
    char *str = &buffer[15];
    *str = '\0';
    do {
	str--;
	*str = '0' + value % 10;
	value /= 10;
    } while (value > 0);
    print_string(str);
}

static void print_hex(uintptr_t value)
{
    char buffer[32];
    char *str = &buffer[31];
    *str = '\0';
    do {
	str--;
	*str = '0' + value % 16;
	if (*str > '9') *str += 'a' - '0' - 10;
	value /= 16;
    } while (value > 0);
    print_string(str);
}

static void wait_for_key(void)
{
    efi_input_key_t input_key;

    while (efi_call_proto(efi_table_attr(sys_table, con_in), read_key_stroke, &input_key) == EFI_NOT_READY) {}
}

static void test_frame_buffer(screen_info_t *si)
{
    uint32_t r_value = 0xffffffff >> (32 - si->red_size);
    uint32_t g_value = 0;
    uint32_t b_value = 0;

    int pixel_size = (si->lfb_depth / 8);

    union {
        uint8_t     byte[4];
        uint32_t    word;
    } pixel_value;

    pixel_value.word = (r_value << si->red_pos) | (g_value << si->green_pos) | (b_value << si->blue_pos);

    uintptr_t lfb_base = si->lfb_base;
#ifdef __x86_64__
    if (LFB_CAPABILITY_64BIT_BASE & si->capabilities) {
        lfb_base |= (uintptr_t)si->ext_lfb_base << 32;
    }
#endif

    uint8_t *lfb_row = (uint8_t *)lfb_base;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < si->lfb_width; x++) {
            for (int b = 0; b < pixel_size; b++) {
                lfb_row[x * pixel_size + b] = pixel_value.byte[b];
            }
        }
        lfb_row += si->lfb_linelength;
    }
    lfb_row += (si->lfb_height - 16) * si->lfb_linelength;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < si->lfb_width; x++) {
            for (int b = 0; b < pixel_size; b++) {
                lfb_row[x * pixel_size + b] = pixel_value.byte[b];
            }
        }
        lfb_row += si->lfb_linelength;
    }
}
#endif

static int get_cmd_line_length(efi_loaded_image_t *image)
{
    // We only use ASCII characters in our command line options, so for simplicity
    // just truncate the command line if we find a non-ASCII character.
    efi_char16_t *cmd_line = (efi_char16_t *)image->load_options;
    int max_length = image->load_options_size / sizeof(efi_char16_t);
    int length = 0;
    while (length < max_length && cmd_line[length] > 0x00 && cmd_line[length] < 0x80) {
        length++;
    }
    return length;
}

static void get_cmd_line(efi_loaded_image_t *image, int num_chars, char *buffer)
{
    efi_char16_t *cmd_line = (efi_char16_t *)image->load_options;
    for (int i = 0; i < num_chars; i++) {
        buffer[i] = cmd_line[i];
    }
    buffer[num_chars] = '\0';
}

static efi_status_t alloc_memory(void **ptr, size_t size, efi_phys_addr_t max_addr)
{
    efi_status_t status;

    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    efi_phys_addr_t addr = max_addr;
    status = efi_call_bs(allocate_pages, EFI_ALLOCATE_MAX_ADDRESS, EFI_LOADER_DATA, num_pages, &addr);
    if (status == EFI_SUCCESS) {
        *ptr = (void *)(uintptr_t)addr;
    }

    return status;
}

static efi_memory_desc_t *get_memory_desc(uintptr_t map_addr, size_t desc_size, size_t n)
{
    return (efi_memory_desc_t *)(map_addr + n * desc_size);
}

static bool map_buffer_has_headroom(size_t buffer_size, size_t map_size, size_t desc_size)
{
    size_t slack = buffer_size - map_size;

    return slack / desc_size >= MAP_BUFFER_HEADROOM;
}

static efi_status_t get_memory_map(
    efi_memory_desc_t  **mem_map,
    uintn_t             *mem_map_size,
    uintn_t             *mem_desc_size,
    uint32_t            *mem_desc_version,
    uintn_t             *mem_map_key,
    uintn_t             *map_buffer_size
)
{
    efi_status_t status;

    *mem_map = NULL;

    *map_buffer_size = *mem_map_size = 32 * sizeof(efi_memory_desc_t);  // for first try

again:
    status = efi_call_bs(allocate_pool, EFI_LOADER_DATA, *map_buffer_size, (void **)mem_map);
    if (status != EFI_SUCCESS) {
        goto fail;
    }

    status = efi_call_bs(get_memory_map, mem_map_size, *mem_map, mem_map_key, mem_desc_size, mem_desc_version);
    if (status == EFI_BUFFER_TOO_SMALL || !map_buffer_has_headroom(*map_buffer_size, *mem_map_size, *mem_desc_size)) {
        efi_call_bs(free_pool, *mem_map);
        // Make sure there is some headroom so that the buffer can be reused
        // for a new map after allocations are no longer permitted. It's
        // unlikely that the map will grow to exceed this headroom once we
        // are ready to trigger ExitBootServices().
        *mem_map_size += *mem_desc_size * MAP_BUFFER_HEADROOM;
        *map_buffer_size = *mem_map_size;
        goto again;
    }
    if (status != EFI_SUCCESS) {
        efi_call_bs(free_pool, *mem_map);
        goto fail;
    }

fail:
    return status;
}

static void get_bit_range(uint32_t mask, uint8_t *pos, uint8_t *size)
{
    int first  = 0;
    int length = 0;

    if (mask) {
        while (!(mask & 0x1)) {
            mask >>= 1;
            first++;
        }
        while (mask & 0x1) {
            mask >>= 1;
            length++;
        }
    }
    *pos  = first;
    *size = length;
}

static efi_graphics_output_t *find_gop(efi_handle_t *handles, size_t handles_size)
{
    efi_status_t status;

    efi_graphics_output_t *first_gop = NULL;
    for (int i = 0; i < efi_get_num_handles(handles_size); i++) {
        efi_handle_t handle = efi_get_handle_at(handles, i);

        efi_graphics_output_t *gop = NULL;
        status = efi_call_bs(handle_protocol, handle, &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, (void **)&gop);
        if (status != EFI_SUCCESS) {
            continue;
        }

        efi_gop_mode_t      *mode = efi_table_attr(gop,  mode);
        efi_gop_mode_info_t *info = efi_table_attr(mode, info);

        // BLT is not available after we call ExitBootServices().
        if (info->pixel_format == PIXEL_BLT_ONLY) {
            continue;
        }

#if DEBUG
        print_string("Found GOP with ");
        print_dec(mode->max_mode);
        print_string(" modes\n");
#endif

        // Systems that use the UEFI Console Splitter may provide multiple GOP
        // devices, not all of which are backed by real hardware. The workaround
        // is to search for a GOP implementing the ConOut protocol, and if one
        // isn't found, to just fall back to the first GOP.

        void *con_out = NULL;
        status = efi_call_bs(handle_protocol, handle, &EFI_CONSOLE_OUT_DEVICE_GUID, &con_out);
        if (status == EFI_SUCCESS) {
#if DEBUG
            print_string("This GOP implements the ConOut protocol\n");
#endif
            return gop;
        }

        if (first_gop == NULL) {
            first_gop = gop;
        }
    }

    return first_gop;
}

static efi_status_t set_screen_info_from_gop(screen_info_t *si, efi_handle_t *handles, size_t handles_size)
{
    efi_status_t status;

    efi_graphics_output_t *gop = find_gop(handles, handles_size);
    if (!gop) {
#if DEBUG
        print_string("GOP not found\n");
#endif
        return EFI_NOT_FOUND;
    }

    efi_gop_mode_t *mode = efi_table_attr(gop, mode);

    efi_gop_mode_info_t best_info;
    best_info.h_resolution = UINT32_MAX;
    best_info.v_resolution = UINT32_MAX;

    uint32_t best_mode = UINT32_MAX;
    for (uint32_t mode_num = 0; mode_num < mode->max_mode; mode_num++) {
        efi_gop_mode_info_t *info;
        uintn_t              info_size;
        status = efi_call_proto(gop, query_mode, mode_num, &info_size, &info);
        if (status != EFI_SUCCESS) {
            continue;
        }

        if (info->h_resolution >= MIN_H_RESOLUTION
         && info->v_resolution >= MIN_V_RESOLUTION
         && info->h_resolution < best_info.h_resolution) {
            best_mode = mode_num;
            best_info = *info;
        }

        efi_call_bs(free_pool, info);
    }
    if (best_mode == UINT32_MAX) {
#if DEBUG
        print_string("No suitable GOP screen resolution\n");
#endif
        return EFI_NOT_FOUND;
    }

    efi_phys_addr_t  lfb_base = efi_table_attr(mode, frame_buffer_base);

    si->orig_video_isVGA = VIDEO_TYPE_EFI;

    si->lfb_width  = best_info.h_resolution;
    si->lfb_height = best_info.v_resolution;
    si->lfb_base   = lfb_base;
#ifdef __x86_64__
    if (lfb_base >> 32) {
        si->capabilities |= LFB_CAPABILITY_64BIT_BASE;
        si->ext_lfb_base = lfb_base >> 32;
    }
#endif

    switch (best_info.pixel_format) {
      case PIXEL_RGB_RESERVED_8BIT_PER_COLOR:
#if DEBUG
        print_string("RGB32 mode\n");
#endif
        si->lfb_depth       = 32;
        si->lfb_linelength  = best_info.pixels_per_scan_line * 4;
        si->red_size        = 8;
        si->red_pos         = 0;
        si->green_size      = 8;
        si->green_pos       = 8;
        si->blue_size       = 8;
        si->blue_pos        = 16;
        si->rsvd_size       = 8;
        si->rsvd_pos        = 24;
        break;
      case PIXEL_BGR_RESERVED_8BIT_PER_COLOR:
#if DEBUG
        print_string("BGR32 mode\n");
#endif
        si->lfb_depth       = 32;
        si->lfb_linelength  = best_info.pixels_per_scan_line * 4;
        si->red_size        = 8;
        si->red_pos         = 16;
        si->green_size      = 8;
        si->green_pos       = 8;
        si->blue_size       = 8;
        si->blue_pos        = 0;
        si->rsvd_size       = 8;
        si->rsvd_pos        = 24;
        break;
      case PIXEL_BIT_MASK:
#if DEBUG
        print_string("Bit mask mode\n");
#endif
        get_bit_range(best_info.pixel_info.red_mask,   &si->red_pos,   &si->red_size);
        get_bit_range(best_info.pixel_info.green_mask, &si->green_pos, &si->green_size);
        get_bit_range(best_info.pixel_info.blue_mask,  &si->blue_pos,  &si->blue_size);
        get_bit_range(best_info.pixel_info.rsvd_mask,  &si->rsvd_pos,  &si->rsvd_size);
        si->lfb_depth       = si->red_size + si->green_size + si->blue_size + si->rsvd_size;
        si->lfb_linelength  = (best_info.pixels_per_scan_line * si->lfb_depth) / 8;
        break;
      default:
#if DEBUG
        print_string("Unsupported mode\n");
#endif
        si->lfb_depth       = 4;
        si->lfb_linelength  = si->lfb_width / 2;
        si->red_size        = 0;
        si->red_pos         = 0;
        si->green_size      = 0;
        si->green_pos       = 0;
        si->blue_size       = 0;
        si->blue_pos        = 0;
        si->rsvd_size       = 0;
        si->rsvd_pos        = 0;
        break;
    }
    si->lfb_size = si->lfb_linelength * si->lfb_height;

#if DEBUG
    print_string("FB base   : ");
    print_hex((uintptr_t)lfb_base);
    print_string("\n");
    print_string("FB size   : ");
    print_dec(si->lfb_width);
    print_string(" x ");
    print_dec(si->lfb_height);
    print_string("\n");
    print_string("FB format :");
    print_string(" R"); print_dec(si->red_size);
    print_string(" G"); print_dec(si->green_size);
    print_string(" B"); print_dec(si->blue_size);
    print_string(" A"); print_dec(si->rsvd_size);
    print_string("\n");
    print_string("FB stride : ");
    print_dec(si->lfb_linelength);
    print_string("\n");
    print_string("Press any key to continue...\n");
    wait_for_key();
#endif

    status = efi_call_proto(gop, set_mode, best_mode);
    if (status != EFI_SUCCESS) {
#if DEBUG
        print_string("Set GOP mode failed\n");
#endif
        return status;
    }

#if DEBUG
    test_frame_buffer(si);
    print_string("Press any key to continue...\n");
    wait_for_key();
#endif

    return EFI_SUCCESS;
}

static efi_status_t set_screen_info(boot_params_t *boot_params)
{
    efi_status_t status;

    uintn_t handles_size = 0;
    efi_handle_t *handles = NULL;
    status = efi_call_bs(locate_handle, EFI_LOCATE_BY_PROTOCOL, &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, NULL,
                         &handles_size, handles);
    if (status == EFI_BUFFER_TOO_SMALL) {
        status = efi_call_bs(allocate_pool, EFI_LOADER_DATA, handles_size, (void **)&handles);
        if (status != EFI_SUCCESS) {
            return status;
        }
        status = efi_call_bs(locate_handle, EFI_LOCATE_BY_PROTOCOL, &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, NULL,
                             &handles_size, handles);
        if (status == EFI_SUCCESS) {
            status = set_screen_info_from_gop(&boot_params->screen_info, handles, handles_size);
        }
        efi_call_bs(free_pool, handles);
    }

    return status;
}

static efi_status_t set_efi_info_and_exit_boot_services(efi_handle_t handle, boot_params_t *boot_params)
{
    efi_status_t status;

    efi_memory_desc_t  *mem_map             = NULL;
    uintn_t             mem_map_size        = 0;
    uintn_t             mem_desc_size       = 0;
    uint32_t            mem_desc_version    = 0;
    uintn_t             mem_map_key         = 0;
    uintn_t             map_buffer_size     = 0;

    status = get_memory_map(&mem_map, &mem_map_size, &mem_desc_size, &mem_desc_version, &mem_map_key, &map_buffer_size);
    if (status != EFI_SUCCESS) {
        goto fail;
    }

    status = efi_call_bs(exit_boot_services, handle, mem_map_key);
    if (status == EFI_INVALID_PARAMETER) {
        // The memory map changed between efi_get_memory_map() and
        // exit_boot_services(). Per the UEFI Spec v2.6, Section 6.4:
        // EFI_BOOT_SERVICES.ExitBootServices, we need to get the
        // updated map, and try again. The spec implies one retry
        // should be sufficent, which is confirmed against the EDK2
        // implementation. Per the spec, we can only invoke
        // get_memory_map() and exit_boot_services() - we cannot alloc
        // so efi_get_memory_map() cannot be used, and we must reuse
        // the buffer. For all practical purposes, the headroom in the
        // buffer should account for any changes in the map so the call
        // to get_memory_map() is expected to succeed here.
        mem_map_size = map_buffer_size;
        status = efi_call_bs(get_memory_map, &mem_map_size, mem_map, &mem_map_key, &mem_desc_size, &mem_desc_version);
        if (status != EFI_SUCCESS) {
            goto fail;
        }

        status = efi_call_bs(exit_boot_services, handle, mem_map_key);
    }
    if (status != EFI_SUCCESS) {
        goto fail;
    }

#ifdef __x86_64
    boot_params->efi_info.loader_signature  = EFI64_LOADER_SIGNATURE;
#else
    boot_params->efi_info.loader_signature  = EFI32_LOADER_SIGNATURE;
#endif
    boot_params->efi_info.sys_tab           = (uintptr_t)sys_table;
    boot_params->efi_info.mem_desc_size     = mem_desc_size;
    boot_params->efi_info.mem_desc_version  = mem_desc_version;
    boot_params->efi_info.mem_map           = (uintptr_t)mem_map;
    boot_params->efi_info.mem_map_size      = mem_map_size;
#ifdef __X86_64__
    boot_params->efi_info.sys_tab_hi        = (uintptr_t)sys_table >> 32;
    boot_params->efi_info.mem_map_hi        = (uintptr_t)mem_map   >> 32;
#endif

fail:
    return status;
}

static void set_e820_map(boot_params_t *params)
{
    uintptr_t mem_map_addr = params->efi_info.mem_map;
#ifdef __X86_64__
    mem_map_addr |= (uintptr_t)params->efi_info.mem_map_hi << 32;
#endif
    size_t mem_map_size  = params->efi_info.mem_map_size;
    size_t mem_desc_size = params->efi_info.mem_desc_size;
    size_t num_descs     = mem_map_size / mem_desc_size;

    e820_entry_t *prev = NULL;
    e820_entry_t *next = params->e820_map;

    int num_entries = 0;
    for (size_t i = 0; i < num_descs && num_entries < E820_MAP_SIZE; i++) {
        efi_memory_desc_t *mem_desc = get_memory_desc(mem_map_addr, mem_desc_size, i);

        e820_type_t e820_type = E820_RESERVED;
        switch (mem_desc->type) {
          case EFI_ACPI_RECLAIM_MEMORY:
            e820_type = E820_ACPI;
            break;
          case EFI_LOADER_CODE:
          case EFI_LOADER_DATA:
          case EFI_BOOT_SERVICES_CODE:
          case EFI_BOOT_SERVICES_DATA:
          case EFI_CONVENTIONAL_MEMORY:
            e820_type = E820_RAM;
            break;
          default:
            continue;
        }

        // Merge adjacent mappings.
        if (prev && prev->type == e820_type && (prev->addr + prev->size) == mem_desc->phys_addr) {
            prev->size += mem_desc->num_pages << PAGE_SHIFT;
            continue;
        }

        next->addr = mem_desc->phys_addr;
        next->size = mem_desc->num_pages << PAGE_SHIFT;
        next->type = e820_type;
        prev = next++;
        num_entries++;
    }
    params->e820_entries = num_entries;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

boot_params_t *efi_setup(efi_handle_t handle, efi_system_table_t *sys_table_arg, boot_params_t *boot_params)
{
    efi_status_t status;

    sys_table = sys_table_arg;
    if (sys_table->header.signature != EFI_SYSTEM_TABLE_SIGNATURE) {
        print_string("bad system table signature\n");
        goto fail;
    }

    if (boot_params == NULL) {
        efi_loaded_image_t *image;
        status = efi_call_bs(handle_protocol, handle, &EFI_LOADED_IMAGE_PROTOCOL_GUID, (void **)&image);
        if (status != EFI_SUCCESS) {
            print_string("failed to get handle for loaded image protocol\n");
            goto fail;
        }

        int cmd_line_length = get_cmd_line_length(image);

        // Allocate below 3GB to avoid having to remap.
        status = alloc_memory((void **)&boot_params, sizeof(boot_params_t) + cmd_line_length + 1, 0xbfffffff);
        if (status != EFI_SUCCESS) {
            print_string("failed to allocate low memory for boot params\n");
            goto fail;
        }
        memset(boot_params, 0, sizeof(boot_params_t));

        uintptr_t cmd_line_addr = (uintptr_t)boot_params + sizeof(boot_params_t);
        get_cmd_line(image, cmd_line_length, (char *)cmd_line_addr);
        boot_params->cmd_line_ptr  = cmd_line_addr;
        boot_params->cmd_line_size = cmd_line_length + 1;
    }

    boot_params->code32_start = (uintptr_t)startup32;

    status = set_screen_info(boot_params);
    if (status != EFI_SUCCESS) {
        print_string("set_screen_info() failed\n");
        goto fail;
    }

    status = set_efi_info_and_exit_boot_services(handle, boot_params);
    if (status != EFI_SUCCESS) {
        print_string("set_efi_info_and_exit_boot_services() failed\n");
        goto fail;
    }

    set_e820_map(boot_params);

    return boot_params;

fail:
    print_string("efi_setup() failed\n");

    while (1) {
        __asm__("hlt");
    }
}
