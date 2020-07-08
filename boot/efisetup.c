// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2020 Martin Whitaker.
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

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAP_BUFFER_HEADROOM     8       // number of descriptors

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static efi_guid_t EFI_CONSOLE_OUT_DEVICE_GUID         = { 0xd3b36f2c, 0xd551, 0x11d4, {0x9a, 0x46, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };
static efi_guid_t EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID   = { 0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a} };

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

#ifdef DEBUG
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
#endif

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

static efi_status_t alloc_low_memory(void **ptr, size_t size, efi_phys_addr_t min_addr)
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

    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t num_descs = mem_map_size / mem_desc_size;

    for (size_t i = 0; i < num_descs; i++) {
        efi_memory_desc_t *desc = get_memory_desc((uintptr_t)mem_map, mem_desc_size, i);

        if (desc->type != EFI_CONVENTIONAL_MEMORY) {
            continue;
        }
        if (desc->num_pages < num_pages) {
            continue;
        }

        efi_phys_addr_t start = desc->phys_addr;
        efi_phys_addr_t end   = start + desc->num_pages * PAGE_SIZE;

        if (start < min_addr) {
            start = min_addr;
        }
        start = round_up(start, PAGE_SIZE);
        if ((start + size) > end) {
            continue;
        }

        status = efi_call_bs(allocate_pages, EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, num_pages, &start);
        if (status == EFI_SUCCESS) {
            *ptr = (void *)(uintptr_t)start;
            efi_call_bs(free_pool, mem_map);
            return EFI_SUCCESS;
        }
    }
    efi_call_bs(free_pool, mem_map);
    status = EFI_NOT_FOUND;

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

static efi_status_t set_screen_info_from_gop(screen_info_t *si, efi_handle_t *handles, size_t handles_size)
{
    efi_status_t status;

    efi_graphics_output_protocol_t *gop = NULL;
    for (int i = 0; i < efi_get_num_handles(handles_size); i++) {
        efi_handle_t handle = efi_get_handle_at(handles, i);

        efi_graphics_output_protocol_t *current_gop = NULL;
        status = efi_call_bs(handle_protocol, handle, &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, (void **)&current_gop);
        if (status != EFI_SUCCESS) {
            continue;
        }

        void *con_out = NULL;
        status = efi_call_bs(handle_protocol, handle, &EFI_CONSOLE_OUT_DEVICE_GUID, &con_out);

        efi_gop_mode_t      *current_mode = efi_table_attr(current_gop,  mode);
        efi_gop_mode_info_t *current_info = efi_table_attr(current_mode, info);

        // Systems that use the UEFI Console Splitter may provide multiple GOP
        // devices, not all of which are backed by real hardware. The workaround
        // is to search for a GOP implementing the ConOut protocol, and if one
        // isn't found, to just fall back to the first GOP.
        if ((!gop || con_out) && current_info->pixel_format != PIXEL_BLT_ONLY) {
            gop = current_gop;
            if (con_out) {
                break;
            }
        }
    }
    if (!gop) {
        return EFI_NOT_FOUND;
    }

    efi_gop_mode_t      *mode = efi_table_attr(gop,  mode);
    efi_gop_mode_info_t *info = efi_table_attr(mode, info);

    efi_phys_addr_t  lfb_base = efi_table_attr(mode, frame_buffer_base);

    si->orig_video_isVGA = VIDEO_TYPE_EFI;

    si->lfb_width  = info->h_resolution;
    si->lfb_height = info->v_resolution;
    si->lfb_base   = lfb_base;
#ifdef __x86_64__
    if (lfb_base >> 32) {
        si->capabilities |= LFB_CAPABILITY_64BIT_BASE;
        si->ext_lfb_base = lfb_base >> 32;
    }
#endif

    switch (info->pixel_format) {
      case PIXEL_RGB_RESERVED_8BIT_PER_COLOR:
        si->lfb_depth       = 32;
        si->lfb_linelength  = info->pixels_per_scan_line * 4;
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
        si->lfb_depth       = 32;
        si->lfb_linelength  = info->pixels_per_scan_line * 4;
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
        si->lfb_depth       = si->red_size + si->green_size + si->blue_size + si->rsvd_size;
        si->lfb_linelength  = (info->pixels_per_scan_line * si->lfb_depth) / 8;
        get_bit_range(info->pixel_info.red_mask,   &si->red_pos,   &si->red_size);
        get_bit_range(info->pixel_info.green_mask, &si->green_pos, &si->green_size);
        get_bit_range(info->pixel_info.blue_mask,  &si->blue_pos,  &si->blue_size);
        get_bit_range(info->pixel_info.rsvd_mask,  &si->rsvd_pos,  &si->rsvd_size);
        break;
      default:
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
    size_t num_descs     = mem_map_size /mem_desc_size;

    e820_entry_t *prev = NULL;
    e820_entry_t *next = params->e820_map;

    int num_entries = 0;
    for (size_t i = 0; i < num_descs && num_entries < E820_MAP_SIZE; i++) {
        efi_memory_desc_t *mem_desc = get_memory_desc(mem_map_addr, mem_desc_size, i);

        e820_type_t e820_type = E820_NONE;
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
        status = alloc_low_memory((void **)&boot_params, sizeof(boot_params_t), 0);
        if (status != EFI_SUCCESS) {
            print_string("failed to allocate low memory for boot params\n");
            goto fail;
        }
        memset(boot_params, 0, sizeof(boot_params_t));
    }

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
