// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Samuel Demeulemeester
//

#include "stdint.h"
#include "string.h"
#include "display.h"
static const uint8_t * table_start = NULL;
static uint32_t table_length = 0; // 16-bit in SMBIOS v2, 32-bit in SMBIOS v3.

#include "boot.h"
#include "bootparams.h"
#include "efi.h"
#include "vmem.h"
#include "smbios.h"

#define LINE_DMI 23

static const efi_guid_t SMBIOS2_GUID = { 0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };

// SMBIOS v3 compliant FW must include an SMBIOS v2 table, but maybe parse SM3 table later...
// static const efi_guid_t SMBIOS3_GUID = { 0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94} };

struct system_info *dmi_system_info;
struct baseboard_info *dmi_baseboard_info;
struct mem_dev *dmi_memory_device;

static char * get_tstruct_string(struct tstruct_header * header, uint16_t maxlen, int n) {
    if (n < 1)
        return NULL;
    char * a = (char *) header + header->length;
    n--;
    do {
        if (! * a)
            n--;
        if (!n && * a)
            return a;
        a++;
    } while (a < ((char *) header + maxlen) && !( * a == 0 && * (a - 1) == 0));
    return NULL;
}

#ifdef __x86_64__
static smbiosv2_t * find_smbiosv2_in_efi64_system_table(efi64_system_table_t * system_table) {
    efi64_config_table_t * config_tables = (efi64_config_table_t *) map_region(system_table->config_tables, system_table->num_config_tables * sizeof(efi64_config_table_t), true);
    if (config_tables == NULL) return NULL;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table->num_config_tables; i++) {
        if (memcmp( & config_tables[i].guid, & SMBIOS2_GUID, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return (smbiosv2_t *) table_addr;
}
#endif

static smbiosv2_t * find_smbiosv2_in_efi32_system_table(efi32_system_table_t * system_table) {
    efi32_config_table_t * config_tables = (efi32_config_table_t *) map_region(system_table->config_tables, system_table->num_config_tables * sizeof(efi32_config_table_t), true);
    if (config_tables == NULL) return NULL;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table->num_config_tables; i++) {
        if (memcmp( & config_tables[i].guid, & SMBIOS2_GUID, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return (smbiosv2_t *) table_addr;
}

static uintptr_t find_smbiosv2_adr(void) {
    const boot_params_t * boot_params = (boot_params_t *) boot_params_addr;
    const efi_info_t * efi_info = & boot_params->efi_info;

    smbiosv2_t * rp = NULL;

    if (efi_info->loader_signature == EFI32_LOADER_SIGNATURE) {
        // EFI32
        if (rp == NULL && efi_info->loader_signature == EFI32_LOADER_SIGNATURE) {
            uintptr_t system_table_addr = map_region(efi_info->sys_tab, sizeof(efi32_system_table_t), true);
            system_table_addr = map_region(system_table_addr, sizeof(efi32_system_table_t), true);
            if (system_table_addr != 0) {
                rp = find_smbiosv2_in_efi32_system_table((efi32_system_table_t *) system_table_addr);
                return (uintptr_t) rp;
            }
        }
    }
#ifdef __x86_64__
    if (rp == NULL && efi_info -> loader_signature == EFI64_LOADER_SIGNATURE) {
        // EFI64
        if (rp == NULL && efi_info->loader_signature == EFI64_LOADER_SIGNATURE) {
            uintptr_t system_table_addr = (uintptr_t) efi_info->sys_tab_hi << 32 | (uintptr_t) efi_info->sys_tab;
            system_table_addr = map_region(system_table_addr, sizeof(efi64_system_table_t), true);
            if (system_table_addr != 0) {
                rp = find_smbiosv2_in_efi64_system_table((efi64_system_table_t *) system_table_addr);
                return (uintptr_t) rp;
            }
        }
    }
#endif
    if (rp == NULL) {
        // BIOS
        uint8_t * dmi, * dmi_search_start;
        dmi_search_start = (uint8_t *) 0x000F0000;

        for (dmi = dmi_search_start; dmi < dmi_search_start + 0xffff0; dmi += 16) {
            if ( * dmi == '_' && * (dmi + 1) == 'S' && * (dmi + 2) == 'M' && * (dmi + 3) == '_')
                return (uintptr_t) dmi;
        }
    }

    return 0;
}

static int parse_dmi(uint16_t numstructs) {
    const uint8_t * dmi = table_start;
    int tstruct_count = 0;

    // Struct type 1 is one of the mandatory types, so we're dealing with invalid data if its size is lower than that of a minimal type 1 struct (plus a couple bytes).
    if (table_length < sizeof(struct system_info)) {
        return -1;
    }

    // Parse all structs (currently restricted to Type 2 only)
    while (dmi < table_start + table_length - 2) { // -2 for header type and length.
        const struct tstruct_header * header = (struct tstruct_header *) dmi;

        // Type 1 - System Information
        if (header->type == 1 && header->length > offsetof(struct system_info, wut)) {
            // Multiple type 1 structs are not allowed by the standard. Still, effectively pick up the last one.
            dmi_system_info = (struct system_info *) dmi;
        }
        // Type 2 - Baseboard Information
        else if (header->type == 2 && header->length > offsetof(struct baseboard_info, serialnumber)) {
            // Multiple type 2 structs are allowed by the standard. Effectively pick up the last one.
            dmi_baseboard_info = (struct baseboard_info *) dmi;
        }
        // Type 17 - Memory Device
        else if (header->type == 17 && header->length > offsetof(struct mem_dev, partnum)) {
            dmi_memory_device = (struct mem_dev *) dmi;
        }

        dmi += header->length;

        if (dmi >= table_start + table_length) {
            dmi_system_info = NULL;
            dmi_baseboard_info = NULL;
            return -1;
        }

        while ((dmi < table_start + table_length - 1) && !(*dmi == 0 && *(dmi + 1) == 0)) {
            dmi++;
        }

        dmi += 2;

        if ((dmi > table_start + table_length) || (++tstruct_count > numstructs)) {
            dmi_system_info = NULL;
            dmi_baseboard_info = NULL;
            return -1;
        }
    }

    return 0;
}

int smbios_init(void) {
    uintptr_t smb_adr;
    const uint8_t * dmi_start;
    const smbiosv2_t * eps;

    // Get SMBIOS Address
    smb_adr = find_smbiosv2_adr();

    if (smb_adr == 0) {
        return -1;
    }

    dmi_start = (const uint8_t *) smb_adr;
    eps = (const smbiosv2_t *) smb_adr;

    // Verify checksum
    int8_t checksum = 0;
    const uint8_t * dmi = dmi_start;

    for (; dmi < (dmi_start + eps->length); dmi++) {
        checksum += * dmi;
    }

    if (checksum) {
        return -1;
    }

    // SMBIOS 2.3 required
    if (eps->majorversion < 2 && eps->minorversion < 3) {
        return -1;
    }

    table_start = (const uint8_t *)(uintptr_t)eps->tableaddress;
    table_length = (uint32_t)eps->tablelength;

    return parse_dmi(eps->numstructs);
}

void print_smbios_startup_info(void) {
    // Use baseboard info (struct type 2) as primary source of information, and fall back to system info (struct type 1).
    // Indeed, while the latter may contain less useful information than the former, its presence is mandated by the successive revisions of the SMBIOS standard.
    // NOTE: we can get away with this ugly cast because the offsets of .manufacturer and .productname are the same in system_info and baseboard_info.
    struct system_info * ptr = dmi_baseboard_info != NULL ? (struct system_info *)dmi_baseboard_info : dmi_system_info;
    if (ptr != NULL) {
        char * sys_man, * sys_sku;

        int sl1, sl2, dmicol;

        sys_man = get_tstruct_string(&ptr->header, table_length - ((uint8_t *)&ptr->header - (uint8_t *)table_start), ptr->manufacturer);
        if (sys_man != NULL) {
            sl1 = strlen(sys_man);

            sys_sku = get_tstruct_string(&ptr->header, table_length - ((uint8_t *)&ptr->header - (uint8_t *)table_start), ptr->productname);
            if (sys_sku != NULL) {
                sl2 = strlen(sys_sku);

                if (sl1 && sl2) {
                    dmicol = 40 - ((sl1 + sl2) / 2);
                    dmicol = prints(LINE_DMI, dmicol, sys_man);
                    prints(LINE_DMI, dmicol + 1, sys_sku);
                }
            }
        }
    }
}
