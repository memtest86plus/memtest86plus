// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022-2026 Samuel Demeulemeester
//

#include "stdint.h"
#include "string.h"
#include "display.h"

#include "boot.h"
#include "bootparams.h"
#include "cpuinfo.h"
#include "efi.h"
#include "vmem.h"
#include "smbios.h"

#define LINE_DMI 23

static const uint8_t *table_start = NULL;
static uint32_t table_length = 0; // 16-bit in SMBIOS v2, 32-bit in SMBIOS v3.

static const efi_guid_t SMBIOS2_GUID = { 0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };

// Some firmware (e.g. QEMU virt, ARM laptops) only publishes the 64-bit SMBIOS v3 entry point.
static const efi_guid_t SMBIOS3_GUID = { 0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94} };

struct system_info *dmi_system_info;
struct baseboard_info *dmi_baseboard_info;
struct mem_dev *dmi_memory_device;
struct cpu_info *dmi_cpu_info;

static char *get_tstruct_string(struct tstruct_header *header, uint16_t maxlen, int n)
{
    if (n < 1)
        return NULL;
    char *a = (char *) header + header->length;
    n--;
    do {
        if (! *a)
            n--;
        if (!n && *a)
            return a;
        a++;
    } while (a < ((char *) header + maxlen) && !( *a == 0 && *(a - 1) == 0));
    return NULL;
}

#if (ARCH_BITS == 64)
static uintptr_t find_in_efi64_system_table(efi64_system_table_t *system_table, const efi_guid_t *guid)
{
    efi64_config_table_t *config_tables = (efi64_config_table_t *) map_region(system_table->config_tables, system_table->num_config_tables * sizeof(efi64_config_table_t), true);
    if (config_tables == NULL) return 0;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table->num_config_tables; i++) {
        if (memcmp( & config_tables[i].guid, guid, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return table_addr;
}
#endif

static uintptr_t find_in_efi32_system_table(efi32_system_table_t *system_table, const efi_guid_t *guid)
{
    efi32_config_table_t *config_tables = (efi32_config_table_t *) map_region(system_table->config_tables, system_table->num_config_tables * sizeof(efi32_config_table_t), true);
    if (config_tables == NULL) return 0;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table->num_config_tables; i++) {
        if (memcmp( & config_tables[i].guid, guid, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return table_addr;
}

static uintptr_t find_efi_config_table(const efi_guid_t *guid)
{
    const boot_params_t *boot_params = (boot_params_t *) boot_params_addr;
    const efi_info_t *efi_info = & boot_params->efi_info;

    if (efi_info->loader_signature == EFI32_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = map_region(efi_info->sys_tab, sizeof(efi32_system_table_t), true);
        if (system_table_addr != 0) {
            return find_in_efi32_system_table((efi32_system_table_t *) system_table_addr, guid);
        }
    }
#if (ARCH_BITS == 64)
    if (efi_info->loader_signature == EFI64_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = (uintptr_t) efi_info->sys_tab_hi << 32 | (uintptr_t) efi_info->sys_tab;
        system_table_addr = map_region(system_table_addr, sizeof(efi64_system_table_t), true);
        if (system_table_addr != 0) {
            return find_in_efi64_system_table((efi64_system_table_t *) system_table_addr, guid);
        }
    }
#endif
    return 0;
}

static uintptr_t find_smbiosv2_adr(void)
{
    uintptr_t rp = find_efi_config_table(&SMBIOS2_GUID);

#if defined(__i386__) || defined(__x86_64__)
    if (rp == 0) {
        // BIOS. Only x86 has a legacy BIOS area; on other architectures
        // this region may not even be mapped.
        uint8_t *dmi, *dmi_search_start;
        dmi_search_start = (uint8_t *) 0x000F0000;

        for (dmi = dmi_search_start; dmi < dmi_search_start + 0xffff0; dmi += 16) {
            if ( *dmi == '_' && *(dmi + 1) == 'S' && *(dmi + 2) == 'M' && *(dmi + 3) == '_')
                return (uintptr_t) dmi;
        }
    }
#endif

    return rp;
}

static int parse_dmi(uint16_t numstructs)
{
    const uint8_t *dmi = table_start;
    int tstruct_count = 0;

    // Struct type 1 is one of the mandatory types, so we're dealing with invalid data
    // if its size is lower than that of a minimal type 1 struct (plus a couple bytes).
    if (table_length < sizeof(struct system_info)) {
        return -1;
    }

    // Parse structs
    while (dmi < table_start + table_length - 2) { // -2 for header type and length.
        const struct tstruct_header *header = (struct tstruct_header *) dmi;

        // Type 127 - End-of-Table. Mandatory with the v3 entry point, whose
        // table length is only an upper bound.
        if (header->type == 127) {
            break;
        }

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
        // Type 4 - Processor Information
        else if (header->type == 4 && header->length > offsetof(struct cpu_info, version)) {
            // One struct per socket; keep the first populated one.
            if (dmi_cpu_info == NULL) {
                dmi_cpu_info = (struct cpu_info *) dmi;
            }
        }
        // Type 17 - Memory Device
        else if (header->type == 17 && header->length > offsetof(struct mem_dev, partnum)) {
            // Multiple type 17 structs are allowed, with unpopulated slots sometimes
            // reported as type 2 (unknown). If type is 0 (uninitialized) or 1/2 (previously
            // initialized with unknown value) => set or overwrite the struct
            if (dmi_memory_device == NULL || dmi_memory_device->type <= 2) {
                dmi_memory_device = (struct mem_dev *) dmi;
            }
        }

        dmi += header->length;

        if (dmi >= table_start + table_length) {
            dmi_system_info = NULL;
            dmi_baseboard_info = NULL;
            dmi_cpu_info = NULL;
            return -1;
        }

        while ((dmi < table_start + table_length - 1) && !(*dmi == 0 && *(dmi + 1) == 0)) {
            dmi++;
        }

        dmi += 2;

        if ((dmi > table_start + table_length) || (++tstruct_count > numstructs)) {
            dmi_system_info = NULL;
            dmi_baseboard_info = NULL;
            dmi_cpu_info = NULL;
            return -1;
        }
    }
    return 0;
}

#if defined(__aarch64__)
// There is no architectural way to get the CPU marketing name on ARM, so
// the MIDR-derived name only identifies the microarchitecture (e.g.
// "Qualcomm Oryon"). Prefer the SMBIOS processor version string (e.g.
// "Snapdragon(R) X Elite - X1E80100") when the firmware provides one.

#define CPU_VERSION_MAX_LEN 50  // display width available for the CPU model

static char cpu_version_str[CPU_VERSION_MAX_LEN + 1];

static void override_cpu_model(void)
{
    if (dmi_cpu_info == NULL) {
        return;
    }

    uint16_t struct_length = table_length - ((uint8_t *)&dmi_cpu_info->header - (uint8_t *)table_start);

    const char *version = get_tstruct_string(&dmi_cpu_info->header, struct_length, dmi_cpu_info->version);
    if (version == NULL) {
        return;
    }

    size_t len = 0;
    int has_content = 0;
    while (version[len] != '\0' && len < CPU_VERSION_MAX_LEN) {
        if (version[len] != ' ') {
            has_content = 1;
        }
        cpu_version_str[len] = version[len];
        len++;
    }
    if (version[len] != '\0') {
        // Truncated: back up to the last word boundary.
        while (len > 0 && cpu_version_str[len - 1] != ' ') {
            len--;
        }
    }
    // Trim trailing spaces and dangling separators.
    while (len > 0 && (cpu_version_str[len - 1] == ' ' || cpu_version_str[len - 1] == '-')) {
        len--;
    }
    cpu_version_str[len] = '\0';

    if (has_content && len > 0) {
        cpu_model = cpu_version_str;
    }
}
#endif

static int8_t table_checksum(const uint8_t *start, uint8_t length)
{
    int8_t checksum = 0;

    for (const uint8_t *p = start; p < (start + length); p++) {
        checksum += *p;
    }
    return checksum;
}

int smbios_init(void)
{
    uintptr_t smb_adr;

    uint64_t dmi_table_addr = 0;
    uint32_t dmi_table_length = 0;
    uint16_t numstructs = 0;

    // Prefer the SMBIOS v2 (32-bit) entry point, which gives an exact struct
    // count, but fall back to the v3 (64-bit) one: some firmware (e.g. QEMU
    // virt, ARM laptops) only publishes the latter.
    smb_adr = find_smbiosv2_adr();
    if (smb_adr != 0) {
        // The entry point lives in firmware-reserved memory, which may not
        // be mapped yet.
        smb_adr = map_region(smb_adr, sizeof(smbiosv2_t), true);
        if (smb_adr == 0) {
            return -1;
        }
        const smbiosv2_t *eps = (const smbiosv2_t *) smb_adr;

        if (table_checksum((const uint8_t *) smb_adr, eps->length) != 0) {
            return -1;
        }

        // SMBIOS 2.3 required
        if (eps->majorversion < 2 && eps->minorversion < 3) {
            return -1;
        }

        dmi_table_addr = eps->tableaddress;
        dmi_table_length = eps->tablelength;
        numstructs = eps->numstructs;
    } else {
        smb_adr = find_efi_config_table(&SMBIOS3_GUID);
        if (smb_adr == 0) {
            return -1;
        }
        smb_adr = map_region(smb_adr, sizeof(smbiosv3_t), true);
        if (smb_adr == 0) {
            return -1;
        }
        const smbiosv3_t *eps = (const smbiosv3_t *) smb_adr;

        if (table_checksum((const uint8_t *) smb_adr, eps->length) != 0) {
            return -1;
        }

        dmi_table_addr = eps->tableaddress;
        dmi_table_length = eps->maxsize;
        // The v3 entry point has no struct count; parsing stops at the
        // mandatory end-of-table struct.
        numstructs = UINT16_MAX;
    }

#if (ARCH_BITS == 32)
    if (dmi_table_addr > UINT32_MAX) {
        return -1;
    }
#endif

    // The DMI structure table also lives in firmware-reserved memory.
    uintptr_t table_addr = map_region((uintptr_t)dmi_table_addr, dmi_table_length, true);
    if (table_addr == 0) {
        return -1;
    }
    table_start = (const uint8_t *)table_addr;
    table_length = dmi_table_length;

    int result = parse_dmi(numstructs);

#if defined(__aarch64__)
    if (result == 0) {
        override_cpu_model();
    }
#endif

    return result;
}

void print_smbios_startup_info(void)
{
    // Use baseboard info (struct type 2) as primary source of information,
    // and fall back to system info (struct type 1). Indeed, while the later
    // may contain less useful information than the former, its presence is
    // mandated by the successive revisions of the SMBIOS standard.
    // NOTE: we can get away with this ugly cast because the offsets of
    // .manufacturer and .productname are the same in system_info and baseboard_info.

    struct system_info *ptr = dmi_baseboard_info != NULL ?
                              (struct system_info *)dmi_baseboard_info : dmi_system_info;

    if (ptr != NULL) {
        char *sys_man, *sys_sku;

        int sl1, sl2, dmicol;

        uint16_t struct_length = table_length - ((uint8_t *)&ptr->header - (uint8_t *)table_start);

        sys_man = get_tstruct_string(&ptr->header, struct_length, ptr->manufacturer);
        if (sys_man != NULL) {
            sl1 = strlen(sys_man);

            sys_sku = get_tstruct_string(&ptr->header, struct_length, ptr->productname);
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
