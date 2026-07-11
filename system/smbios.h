// SPDX-License-Identifier: GPL-2.0
#ifndef SMBIOS_H
#define SMBIOS_H
/**
 * \file
 *
 * Provides functions for reading SMBIOS tables
 *
 * Copyright (C) 2004-2026 Samuel Demeulemeester.
 */

#define DMI_SDR         0x0F
#define DMI_RDRAM       0x11
#define DMI_DDR         0x12
#define DMI_DDR2        0x13
#define DMI_DDR2_FBDIMM 0x14
#define DMI_DDR3        0x18
#define DMI_DDR4        0x1A
#define DMI_LPDDR       0x1B
#define DMI_LPDDR2      0x1C
#define DMI_LPDDR3      0x1D
#define DMI_LPDDR4      0x1E
#define DMI_DDR5        0x22
#define DMI_LPDDR5      0x23

typedef struct {
    uint8_t anchor[4];
    int8_t checksum;
    uint8_t length;
    uint8_t majorversion;
    uint8_t minorversion;
    uint16_t maxstructsize;
    uint8_t revision;
    uint8_t pad[5];
    uint8_t intanchor[5];
    int8_t intchecksum;
    uint16_t tablelength;
    uint32_t tableaddress;
    uint16_t numstructs;
    uint8_t SMBIOSrev;
} smbiosv2_t;

typedef struct {
    uint8_t anchor[5];  // "_SM3_"
    int8_t checksum;
    uint8_t length;
    uint8_t majorversion;
    uint8_t minorversion;
    uint8_t docrev;
    uint8_t revision;
    uint8_t reserved;
    uint32_t maxsize;
    uint64_t tableaddress;
} __attribute__((packed)) smbiosv3_t;

struct tstruct_header {
    uint8_t type;
    uint8_t length;
    uint16_t handle;
} __attribute__((packed));

struct system_info {
    struct tstruct_header header;
    uint8_t  manufacturer;
    uint8_t  productname;
    uint8_t  version;
    uint8_t  serialnumber;
    uint8_t  uuidbytes[16];
    uint8_t  wut; // Last field defined by SMBIOS 2.3.
    /*uint8_t  sku_number;
    uint8_t  family;*/
} __attribute__((packed));

struct baseboard_info {
    struct tstruct_header header;
    uint8_t  manufacturer;
    uint8_t  productname;
    uint8_t  version;
    uint8_t  serialnumber; // Last field defined by SMBIOS 2.3.
    /*uint8_t  asset_tag;
    uint8_t  feature_flags;
    uint8_t  location_in_chassis;
    uint16_t chassis_handle;
    uint8_t  board_type;
    uint16_t number_contained_object_handles;*/
} __attribute__((packed));

struct cpu_info {
    struct tstruct_header header;
    uint8_t  socket_designation;
    uint8_t  cpu_type;
    uint8_t  family;
    uint8_t  manufacturer;
    uint8_t  cpuid[8];
    uint8_t  version;
    uint8_t  voltage;
    uint16_t ext_clock;
    uint16_t max_speed;
    uint16_t cur_speed;
    uint8_t  status;
    uint8_t  upgrade; // Last field defined by SMBIOS 2.0.
    /*uint16_t l1_handle;
    uint16_t l2_handle;
    uint16_t l3_handle;
    uint8_t  serialnumber;
    uint8_t  asset_tag;
    uint8_t  partnumber;*/
} __attribute__((packed));

struct mem_module {
    struct tstruct_header header;
    uint8_t  socket_designation;
    uint8_t  bank_connections;
    uint8_t  current_speed;
    uint16_t current_memory_type;
    uint8_t  installed_size;
    uint8_t  enabled_size;
    uint8_t  error_status;
} __attribute__((packed));

struct mem_dev {
    struct tstruct_header header;
    uint16_t pma_handle;
    uint16_t err_handle;
    uint16_t tot_width;
    uint16_t dat_width;
    uint16_t size;
    uint8_t  form;
    uint8_t  set;
    uint8_t  dev_locator;
    uint8_t  bank_locator;
    uint8_t  type;
    uint16_t typedetail;
    uint16_t speed;
    uint8_t  manufacturer;
    uint8_t  serialnum;
    uint8_t  asset;
    uint8_t  partnum; // Last field defined by SMBIOS 2.3.
    uint8_t  attributes; // Last field defined by SMBIOS 2.6.
    uint32_t ext_size;
    uint16_t conf_ram_speed; // Last field defined by SMBIOS 2.7.
    uint16_t min_voltage;
    uint16_t max_voltage;
    uint16_t conf_voltage; // Last field defined by SMBIOS 2.8.
    uint8_t  technology;
    uint16_t operating_mode_capability;
    uint8_t  firmware_version;
    uint16_t module_manufacturer_id;
    uint16_t module_product_id;
    uint16_t mem_subsystem_controller_manufacturer_id;
    uint16_t mem_subsystem_controller_product_id;
    uint64_t nonvolatile_size;
    uint64_t volatile_size;
    uint64_t cache_size;
    uint64_t logical_size; // Last field defined by SMBIOS 3.2.
    uint32_t extended_speed;
    uint32_t extended_conf_speed; // Last field defined by SMBIOS 3.3.
} __attribute__((packed));

/**
 * Memory device Structure (used for SPD decoding)
 */

extern struct mem_dev *dmi_memory_device;

/**
 * Cached copy of dmi_memory_device->type (0 = undefined). Use this after
 * startup: pointers into the SMBIOS table do not survive relocation.
 */

extern uint8_t dmi_memory_device_type;

/**
 * Maximum number of SMBIOS Type 17 (Memory Device) structs collected.
 * Pointers only, so this is cheap; 32 covers 2-socket servers with
 * 24-32 DIMM slots. Devices beyond the limit are ignored.
 */

#define MAX_DMI_MEM_DEVICES 32

extern struct mem_dev *dmi_memory_devices[MAX_DMI_MEM_DEVICES];
extern int dmi_num_memory_devices;

/**
 * Processor Information Structure (SMBIOS type 4)
 */

extern struct cpu_info *dmi_cpu_info;

/**
 * Initialize SMBIOS/DMI (locate struct)
 */

int smbios_init(void);

/**
 * Retrieve board manufacturer and product name strings.
 * Sets output pointers to NULL if unavailable.
 */
void get_smbios_board_info(const char **manufacturer, const char **product);

/**
 * Print DMI
 */

void print_smbios_startup_info(void);

/**
 * Print per-module memory info from DMI Type 17 structs, as a fallback
 * when SPD decoding found no module. Prints nothing if no populated
 * Type 17 struct exists.
 */

void print_dmi_memory_info(void);

#endif // SMBIOS_H
