// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Samuel Demeulemeester
//

#include "display.h"
#include "stdint.h"
#include "string.h"

#include "bootparams.h"
#include "efi.h"
#include "vmem.h"
#include "smbios.h"

#define LINE_DMI 23

static const efi_guid_t SMBIOS2_GUID = { 0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };

// SMBIOS v3 compliant FW must include an SMBIOS v2 table, but maybe parse SM3 table later...
// static const efi_guid_t SMBIOS3_GUID = { 0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94} };

struct system_map *dmi_system_info;
struct mem_dev *dmi_memory_device;

char * get_tstruct_string(struct tstruct_header * header, int n) {
    if (n < 1)
        return 0;
    char * a = (char * ) header + header -> length;
    n--;
    do {
        if (! * a)
            n--;
        if (!n && * a)
            return a;
        a++;
    } while (!( * a == 0 && * (a - 1) == 0));
    return 0;
}

#ifdef __x86_64__
static smbios_t * find_smbios_in_efi64_system_table(efi64_system_table_t * system_table) {
    efi64_config_table_t * config_tables = (efi64_config_table_t * ) map_region(system_table -> config_tables, system_table -> num_config_tables * sizeof(efi64_config_table_t), true);
    if (config_tables == NULL) return NULL;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table -> num_config_tables; i++) {
        if (memcmp( & config_tables[i].guid, & SMBIOS2_GUID, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return (smbios_t * ) table_addr;
}
#endif

static smbios_t * find_smbios_in_efi32_system_table(efi32_system_table_t * system_table) {
    efi32_config_table_t * config_tables = (efi32_config_table_t * ) map_region(system_table -> config_tables, system_table -> num_config_tables * sizeof(efi32_config_table_t), true);
    if (config_tables == NULL) return NULL;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table -> num_config_tables; i++) {
        if (memcmp( & config_tables[i].guid, & SMBIOS2_GUID, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return (smbios_t * ) table_addr;
}

static uintptr_t find_smbios_adr(void) {
    const boot_params_t * boot_params = (boot_params_t * ) boot_params_addr;
    const efi_info_t * efi_info = & boot_params -> efi_info;

    smbios_t * rp = NULL;

    if (efi_info -> loader_signature == EFI32_LOADER_SIGNATURE) {
        // EFI32
        if (rp == NULL && efi_info -> loader_signature == EFI32_LOADER_SIGNATURE) {
            uintptr_t system_table_addr = map_region(efi_info -> sys_tab, sizeof(efi32_system_table_t), true);
            system_table_addr = map_region(system_table_addr, sizeof(efi32_system_table_t), true);
            if (system_table_addr != 0) {
                rp = find_smbios_in_efi32_system_table((efi32_system_table_t * ) system_table_addr);
                return (uintptr_t) rp;
            }
        }
    }
#ifdef __x86_64__
    if (rp == NULL && efi_info -> loader_signature == EFI64_LOADER_SIGNATURE) {
        // EFI64
        if (rp == NULL && efi_info -> loader_signature == EFI64_LOADER_SIGNATURE) {
            uintptr_t system_table_addr = (uintptr_t) efi_info -> sys_tab_hi << 32 | (uintptr_t) efi_info -> sys_tab;
            system_table_addr = map_region(system_table_addr, sizeof(efi64_system_table_t), true);
            if (system_table_addr != 0) {
                rp = find_smbios_in_efi64_system_table((efi64_system_table_t * ) system_table_addr);
                return (uintptr_t) rp;
            }
        }
    }
#endif
    if (rp == NULL) {
        // BIOS
        char * dmi, * dmi_search_start;
        dmi_search_start = (char * ) 0x000F0000;

        for (dmi = dmi_search_start; dmi < dmi_search_start + 0xffff0; dmi += 16) {
            if ( * dmi == '_' && * (dmi + 1) == 'S' && * (dmi + 2) == 'M' && * (dmi + 3) == '_')
                return (uintptr_t) dmi;
        }
    }

    return 0;
}

int smbios_init(void) {

    char * dmi, * dmi_start, *table_start;
    int tstruct_count = 0;
    uintptr_t smb_adr;
    smbios_t * eps;

    // Get SMBIOS Address
    smb_adr = find_smbios_adr();

    if (smb_adr == 0) {
        return -1;
    }

    dmi_start = (char * ) smb_adr;
    eps = (smbios_t * ) smb_adr;

    // Verify checksum
    int8_t checksum = 0;

    for (; dmi < (dmi_start + eps->length); dmi++) {
        checksum += * dmi;
    }

    if (checksum) {
        return -1;
    }

    // SMBIOS 2.3 required
    if (eps -> majorversion < 2 && eps -> minorversion < 3) {
        return -1;
    }

    table_start = (char * )(uintptr_t) eps -> tableaddress;
    dmi = (char * ) table_start;

    // Parse all structs
    while (dmi < (table_start + eps -> tablelength)) {
        struct tstruct_header * header = (struct tstruct_header *) dmi;

        // Type 2 - Baseboard Information
        if (header -> type == 2) {
            dmi_system_info = (struct system_map *) dmi;
        }

        // Type 17 - Memory Device
        if (header->type == 17) {
            dmi_memory_device = (struct mem_dev *) dmi;
        }

        dmi += header -> length;

        while (!(*dmi == 0 && *(dmi + 1) == 0)) {
            dmi++;
        }

        dmi += 2;

        if (++tstruct_count > eps->numstructs) {
            return -1;
        }
    }

    return 0;
}

void print_smbios_startup_info(void) {
    char *sys_man, *sys_sku;

    int sl1, sl2, dmicol;

    sys_man = get_tstruct_string(&dmi_system_info->header, dmi_system_info->manufacturer);
    sl1 = strlen(sys_man);

    sys_sku = get_tstruct_string(&dmi_system_info->header, dmi_system_info->productname);
    sl2 = strlen(sys_sku);

    if (sl1 && sl2) {
        dmicol = 40 - ((sl1 + sl2) / 2);
        display_dmi_mb(sys_man, sys_sku)
    }
}
