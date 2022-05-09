// SPDX-License-Identifier: GPL-2.0
#ifndef EFI_H
#define EFI_H
/**
 * \file
 *
 * Provides definitions for accessing the UEFI boot services and configuration
 * tables.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdint.h>

#ifdef __x86_64__
#define NATIVE_MSB              UINT64_C(0x8000000000000000)
#else
#define NATIVE_MSB              0x80000000
#endif

/**
 * EFI_STATUS values.
 */
#define EFI_SUCCESS             0
#define EFI_INVALID_PARAMETER   (NATIVE_MSB |  2)
#define EFI_UNSUPPORTED         (NATIVE_MSB |  3)
#define EFI_BUFFER_TOO_SMALL    (NATIVE_MSB |  5)
#define EFI_NOT_READY           (NATIVE_MSB |  6)
#define EFI_NOT_FOUND           (NATIVE_MSB | 14)
#define EFI_ABORTED             (NATIVE_MSB | 21)

/**
 * EFI_LOCATE_SEARCH_TYPE values.
 */
#define EFI_LOCATE_BY_PROTOCOL  2

/**
 * EFI_ALLOCATE_TYPE values.
 */
#define EFI_ALLOCATE_MAX_ADDRESS    1
#define EFI_ALLOCATE_ADDRESS        2

/**
 * EFI_MEMORY_TYPE values.
 */
#define EFI_LOADER_CODE         1
#define EFI_LOADER_DATA         2
#define EFI_BOOT_SERVICES_CODE  3
#define EFI_BOOT_SERVICES_DATA  4
#define EFI_CONVENTIONAL_MEMORY 7
#define EFI_ACPI_RECLAIM_MEMORY 9

/**
 * EFI_RESET_TYPE values.
 */
#define EFI_RESET_COLD          0
#define EFI_RESET_WARM          1
#define EFI_RESET_SHUTDOWN      2

/**
 * EFI_GRAPHICS_PIXEL_FORMAT values.
 */
#define PIXEL_RGB_RESERVED_8BIT_PER_COLOR   0
#define PIXEL_BGR_RESERVED_8BIT_PER_COLOR   1
#define PIXEL_BIT_MASK                      2
#define PIXEL_BLT_ONLY                      3

#define EFI_SYSTEM_TABLE_SIGNATURE      UINT64_C(0x5453595320494249)
#define EFI_RUNTIME_SERVICES_SIGNATURE  UINT64_C(0x5652453544e5552)

#define efiapi __attribute__((ms_abi))

#ifdef __x86_64__
typedef uint64_t        uintn_t;
#else
typedef uint32_t        uintn_t;
#endif

typedef void *          efi_handle_t;
typedef uintn_t         efi_status_t;
typedef uint64_t        efi_phys_addr_t;
typedef uint64_t        efi_virt_addr_t;
typedef uint16_t        efi_char16_t;

typedef struct {
    uint32_t            a;
    uint16_t            b;
    uint16_t            c;
    uint8_t             d[8];
} efi_guid_t;

typedef struct {
    uint32_t            type;
    uint32_t            pad;
    efi_phys_addr_t     phys_addr;
    efi_virt_addr_t     virt_addr;
    uint64_t            num_pages;
    uint64_t            attribute;
} efi_memory_desc_t;

typedef struct {
    uint32_t            red_mask;
    uint32_t            green_mask;
    uint32_t            blue_mask;
    uint32_t            rsvd_mask;
} efi_pixel_bitmask_t;

typedef struct {
    uint32_t            version;
    uint32_t            h_resolution;
    uint32_t            v_resolution;
    int                 pixel_format;
    efi_pixel_bitmask_t pixel_info;
    uint32_t            pixels_per_scan_line;
} efi_gop_mode_info_t;

typedef struct {
    uint32_t            max_mode;
    uint32_t            mode;
    efi_gop_mode_info_t *info;
    uintn_t             info_size;
    efi_phys_addr_t     frame_buffer_base;
    uintn_t             frame_buffer_size;
} efi_gop_mode_t;

typedef struct efi_graphics_output_s {
    efi_status_t        (efiapi *query_mode)(struct efi_graphics_output_s *, uint32_t, uintn_t *, efi_gop_mode_info_t **);
    efi_status_t        (efiapi *set_mode)(struct efi_graphics_output_s *, uint32_t);
    void                *blt;
    efi_gop_mode_t      *mode;
} efi_graphics_output_t;

typedef struct {
    uint64_t            signature;
    uint32_t            revision;
    uint32_t            header_size;
    uint32_t            crc32;
    uint32_t            reserved;
} efi_table_header_t;

typedef struct {
    uint16_t            scan_code;
    efi_char16_t        ch;
} efi_input_key_t;

typedef struct efi_simple_text_in_s {
    void                *reset;
    efi_status_t        (efiapi *read_key_stroke)(struct efi_simple_text_in_s *, efi_input_key_t *);
    void                *test_string;
} efi_simple_text_in_t;

typedef struct efi_simple_text_out_s {
    void                *reset;
    efi_status_t        (efiapi *output_string)(struct efi_simple_text_out_s *, efi_char16_t *);
    void                *test_string;
} efi_simple_text_out_t;

typedef struct {
    efi_table_header_t  header;
    void                *raise_tpl;
    void                *restore_tpl;
    efi_status_t        (efiapi *allocate_pages)(int, int, uintn_t, efi_phys_addr_t *);
    efi_status_t        (efiapi *free_pages)(efi_phys_addr_t, uintn_t);
    efi_status_t        (efiapi *get_memory_map)(uintn_t *, void *, uintn_t *, uintn_t *, uint32_t *);
    efi_status_t        (efiapi *allocate_pool)(int, uintn_t, void **);
    efi_status_t        (efiapi *free_pool)(void *);
    void                *create_event;
    void                *set_timer;
    void                *wait_for_event;
    void                *signal_event;
    void                *close_event;
    void                *check_event;
    void                *install_protocol_interface;
    void                *reinstall_protocol_interface;
    void                *uninstall_protocol_interface;
    efi_status_t        (efiapi *handle_protocol)(efi_handle_t, efi_guid_t *, void **);
    void                *reserved;
    void                *register_protocol_notify;
    efi_status_t        (efiapi *locate_handle)(int, efi_guid_t *, void *, uintn_t *, efi_handle_t *);
    void                *locate_device_path;
    efi_status_t        (efiapi *install_configuration_table)(efi_guid_t *, void *);
    void                *load_image;
    void                *start_image;
    void                *exit;
    void                *unload_image;
    efi_status_t        (efiapi *exit_boot_services)(efi_handle_t, uintn_t);
    void                *get_next_monotonic_count;
    void                *stall;
    void                *set_watchdog_timer;
    void                *connect_controller;
    efi_status_t        (efiapi *disconnect_controller)(efi_handle_t, efi_handle_t, efi_handle_t);
    void                *open_protocol;
    void                *close_protocol;
    void                *open_protocol_information;
    void                *protocols_per_handle;
    void                *locate_handle_buffer;
    efi_status_t        (efiapi *locate_protocol)(efi_guid_t *, void *, void **);
    void                *install_multiple_protocol_interfaces;
    void                *uninstall_multiple_protocol_interfaces;
    void                *calculate_crc32;
    void                *copy_mem;
    void                *set_mem;
    void                *create_event_ex;
} efi_boot_services_t;

typedef struct {
    efi_table_header_t  header;
    unsigned long       get_time;
    unsigned long       set_time;
    unsigned long       get_wakeup_time;
    unsigned long       set_wakeup_time;
    unsigned long       set_virtual_address_map;
    unsigned long       convert_pointer;
    unsigned long       get_variable;
    unsigned long       get_next_variable;
    unsigned long       set_variable;
    unsigned long       get_next_high_mono_count;
    efi_status_t        (efiapi *reset_system)(int, int, int);
    unsigned long       update_capsule;
    unsigned long       query_capsule_caps;
    unsigned long       query_variable_info;
} efi_runtime_services_t;

typedef struct {
    efi_guid_t          guid;
    uint32_t            table;
} efi32_config_table_t;

typedef struct {
    efi_guid_t          guid;
    uint64_t            table;
} efi64_config_table_t;

typedef struct {
    efi_guid_t          guid;
    void                *table;
} efi_config_table_t;

typedef struct {
    efi_table_header_t  header;
    uint32_t            fw_vendor;
    uint32_t            fw_revision;
    uint32_t            con_in_handle;
    uint32_t            con_in;
    uint32_t            con_out_handle;
    uint32_t            con_out;
    uint32_t            std_err_handle;
    uint32_t            std_err;
    uint32_t            runtime_services;
    uint32_t            boot_services;
    uint32_t            num_config_tables;
    uint32_t            config_tables;
} efi32_system_table_t;

typedef struct {
    efi_table_header_t  header;
    uint64_t            fw_vendor;
    uint32_t            fw_revision;
    uint32_t            pad;
    uint64_t            con_in_handle;
    uint64_t            con_in;
    uint64_t            con_out_handle;
    uint64_t            con_out;
    uint64_t            std_err_handle;
    uint64_t            std_err;
    uint64_t            runtime_services;
    uint64_t            boot_services;
    uint64_t            num_config_tables;
    uint64_t            config_tables;
} efi64_system_table_t;

typedef struct {
    efi_table_header_t      header;
    efi_char16_t           *fw_vendor;
    uint32_t                fw_revision;
    efi_handle_t            con_in_handle;
    efi_simple_text_in_t   *con_in;
    efi_handle_t            con_out_handle;
    efi_simple_text_out_t  *con_out;
    efi_handle_t            std_err_handle;
    efi_simple_text_out_t  *std_err;
    efi_runtime_services_t *runtime_services;
    efi_boot_services_t    *boot_services;
    uintn_t                 num_config_tables;
    efi_config_table_t     *config_tables;
} efi_system_table_t;

typedef struct {
    uint32_t            revision;
    efi_handle_t        parent_handle;
    efi_system_table_t *system_table;
    efi_handle_t        device_handle;
    void               *file_path;
    void               *reserved;
    uint32_t            load_options_size;
    void               *load_options;
    void               *image_base;
    uint64_t            image_size;
    int                 image_code_type;
    int                 image_data_type;
    void               *unload;
} efi_loaded_image_t;

#endif /* EFI_H */
