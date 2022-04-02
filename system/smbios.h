// SPDX-License-Identifier: GPL-2.0
#ifndef SMBIOS_H
#define SMBIOS_H
/**
 * \file
 *
 * Provides functions for reading SMBIOS tables
 *
 * Copyright (C) 2004-2022 Samuel Demeulemeester.
 */

#define DMI_SDR     0x0F
#define DMI_DDR     0x12
#define DMI_DDR2    0x13
#define DMI_DDR3    0x18
#define DMI_DDR4    0x1A
#define DMI_DDR5    0x22

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

struct tstruct_header {
    uint8_t type;
    uint8_t length;
    uint16_t handle;
} __attribute__((packed));

struct system_map {
    struct tstruct_header header;
    uint8_t  manufacturer;
    uint8_t  productname;
    uint8_t  version;
    uint8_t  serialnumber; // Last field defined by SMBIOS 2.3.
    uint8_t  asset_tag;
    uint8_t  feature_flags;
    uint8_t  location_in_chassis;
    uint16_t chassis_handle;
    uint8_t  board_type;
    uint16_t number_contained_object_handles;
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
    uint8_t  attributes;
    uint8_t  ext_size;
    uint8_t  conf_ram_speed;
    uint8_t  min_voltage;
    uint8_t  max_votage;
    uint8_t  conf_voltage;
} __attribute__((packed));

/**
 * Memory device Structure (used for SPD decoding)
 */

extern struct mem_dev *dmi_memory_device;

/**
 * Initialize SMBIOS/DMI (locate struct)
 */

int smbios_init(void);

/**
 * Print DMI 
 */

void print_smbios_startup_info(void);

#endif // SMBIOS_H
