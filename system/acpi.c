// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2022 Sam Demeulemeester.

#include "boot.h"
#include "bootparams.h"
#include "efi.h"

#include "pmem.h"
#include "string.h"
#include "unistd.h"
#include "vmem.h"

#include "acpi.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Table signatures

#define RSDPSignature1  ('R' | ('S' << 8) | ('D' << 16) | (' ' << 24))
#define RSDPSignature2  ('P' | ('T' << 8) | ('R' << 16) | (' ' << 24))

#define RSDTSignature   ('R' | ('S' << 8) | ('D' << 16) | ('T' << 24))

#define XSDTSignature   ('X' | ('S' << 8) | ('D' << 16) | ('T' << 24))

#define MADTSignature   ('A' | ('P' << 8) | ('I' << 16) | ('C' << 24))

#define FADTSignature   ('F' | ('A' << 8) | ('C' << 16) | ('P' << 24))

#define HPETSignature   ('H' | ('P' << 8) | ('E' << 16) | ('T' << 24))

#define EINJSignature   ('E' | ('I' << 8) | ('N' << 16) | ('J' << 24))

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct {
    char        signature[8];   // "RSD PTR "
    uint8_t     checksum;
    char        oem_id[6];
    uint8_t     revision;
    uint32_t    rsdt_addr;
    uint32_t    length;
    uint64_t    xsdt_addr;
    uint8_t     xchecksum;
    uint8_t     reserved[3];
} rsdp_t;

typedef struct {
    char        signature[4];   // "RSDT" or "XSDT"
    uint32_t    length;
    uint8_t     revision;
    uint8_t     checksum;
    char        oem_id[6];
    char        oem_table_id[8];
    char        oem_revision[4];
    char        creator_id[4];
    char        creator_revision[4];
} rsdt_header_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static const efi_guid_t EFI_ACPI_1_RDSP_GUID = { 0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };
static const efi_guid_t EFI_ACPI_2_RDSP_GUID = { 0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81} };

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

const char *rsdp_source = "";

uintptr_t rsdp_addr = 0;
uintptr_t madt_addr = 0;
uintptr_t fadt_addr = 0;
uintptr_t hpet_addr = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static rsdp_t *scan_for_rsdp(uintptr_t addr, int length)
{
    uint32_t *ptr = (uint32_t *)addr;
    uint32_t *end = ptr + length / sizeof(uint32_t);

    while (ptr < end) {
        rsdp_t *rp = (rsdp_t *)ptr;
        if (*ptr == RSDPSignature1 && *(ptr+1) == RSDPSignature2 && acpi_checksum(ptr, 20) == 0) {
            if (rp->revision < 2 || (rp->length < 1024 && acpi_checksum(ptr, rp->length) == 0)) {
                return rp;
            }
        }
        ptr += 4;
    }
    return NULL;
}

#ifdef __x86_64__
static rsdp_t *find_rsdp_in_efi64_system_table(efi64_system_table_t *system_table)
{
    efi64_config_table_t *config_tables = (efi64_config_table_t *)map_region(system_table->config_tables,
                                                                             system_table->num_config_tables * sizeof(efi64_config_table_t),
                                                                             true);
    if (config_tables == NULL) return NULL;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table->num_config_tables; i++) {
        if (memcmp(&config_tables[i].guid, &EFI_ACPI_2_RDSP_GUID, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
            break;
        }
        if (memcmp(&config_tables[i].guid, &EFI_ACPI_1_RDSP_GUID, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return (rsdp_t *)table_addr;
}
#else
static rsdp_t *find_rsdp_in_efi32_system_table(efi32_system_table_t *system_table)
{
    efi32_config_table_t *config_tables = (efi32_config_table_t *)map_region(system_table->config_tables,
                                                                             system_table->num_config_tables * sizeof(efi32_config_table_t),
                                                                             true);
    if (config_tables == NULL) return NULL;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table->num_config_tables; i++) {
        if (memcmp(&config_tables[i].guid, &EFI_ACPI_2_RDSP_GUID, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
            break;
        }
        if (memcmp(&config_tables[i].guid, &EFI_ACPI_1_RDSP_GUID, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return (rsdp_t *)table_addr;
}
#endif

static uintptr_t find_rsdp(void)
{
    const boot_params_t *boot_params = (boot_params_t *)boot_params_addr;

    const efi_info_t *efi_info = &boot_params->efi_info;

    // Search for the RSDP
    rsdp_t *rp = NULL;
#ifdef __x86_64__
    if (efi_info->loader_signature == EFI64_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = (uintptr_t)efi_info->sys_tab_hi << 32 | (uintptr_t)efi_info->sys_tab;
        system_table_addr = map_region(system_table_addr, sizeof(efi64_system_table_t), true);
        if (system_table_addr != 0) {
            rp = find_rsdp_in_efi64_system_table((efi64_system_table_t *)system_table_addr);
            if (rp) rsdp_source = "EFI64 system table";
        }
    }
#else
    if (efi_info->loader_signature == EFI32_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = map_region(efi_info->sys_tab, sizeof(efi32_system_table_t), true);
        if (system_table_addr != 0) {
            rp = find_rsdp_in_efi32_system_table((efi32_system_table_t *)system_table_addr);
            if (rp) rsdp_source = "EFI32 system table";
        }
    }
#endif
    if (rp == NULL) {
        // Search the BIOS EBDA area.
        uintptr_t address = *(uint16_t *)0x40E << 4;
        if (address) {
            rp = scan_for_rsdp(address, 0x400);
            if (rp) rsdp_source = "BIOS EBDA";
        }
    }
    if (rp == NULL) {
        // Search the BIOS reserved area.
        rp = scan_for_rsdp(0xE0000, 0x20000);
        if (rp) rsdp_source = "BIOS reserved area";
    }
    if (rp == NULL) {
        // RSDP not found, give up.
        return 0;
    }
    return (uintptr_t)rp;
}

static uintptr_t find_acpi_table(uint32_t table_signature)
{
   rsdp_t *rp = (rsdp_t *)rsdp_addr;;

    // Found the RSDP, now get either the RSDT or XSDT and scan it for a pointer to the MADT.
    rsdt_header_t *rt;

    if (rp->revision >= 2) {
        rt = (rsdt_header_t *)map_region(rp->xsdt_addr, sizeof(rsdt_header_t), true);
        if (rt == NULL) {
            return 0;
        }
        // Validate the XSDT.
        if (*(uint32_t *)rt != XSDTSignature) {
            return 0;
        }
        rt = (rsdt_header_t *)map_region(rp->xsdt_addr, rt->length, true);
        if (rt == NULL || acpi_checksum(rt, rt->length) != 0) {
            return 0;
        }
        // Scan the XSDT for a pointer to the MADT.
        uint64_t *tab_ptr = (uint64_t *)((uint8_t *)rt + sizeof(rsdt_header_t));
        uint64_t *tab_end = (uint64_t *)((uint8_t *)rt + rt->length);

        while (tab_ptr < tab_end) {
            uintptr_t addr = *tab_ptr++;  // read the next table entry
            uint32_t *ptr = (uint32_t *)map_region(addr, sizeof(uint32_t), true);

            if (ptr && *ptr == table_signature) {
                return addr;
            }
        }
    } else {
        rt = (rsdt_header_t *)map_region(rp->rsdt_addr, sizeof(rsdt_header_t), true);
        if (rt == NULL) {
            return 0;
        }
        // Validate the RSDT.
        if (*(uint32_t *)rt != RSDTSignature) {
            return 0;
        }
        rt = (rsdt_header_t *)map_region(rp->rsdt_addr, rt->length, true);
        if (rt == NULL || acpi_checksum(rt, rt->length) != 0) {
            return 0;
        }
        // Scan the RSDT for a pointer to the MADT.
        uint32_t *tab_ptr = (uint32_t *)((uint8_t *)rt + sizeof(rsdt_header_t));
        uint32_t *tab_end = (uint32_t *)((uint8_t *)rt + rt->length);

        while (tab_ptr < tab_end) {
            uintptr_t addr = *tab_ptr++;  // read the next table entry
            uint32_t *ptr = (uint32_t *)map_region(addr, sizeof(uint32_t), true);

            if (ptr && *ptr == table_signature) {
                return addr;
            }
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int acpi_checksum(const void *data, int length)
{
    uint8_t sum = 0;

    uint8_t *ptr = (uint8_t *)data;
    while (length--) {
        sum += *ptr++;
    }
    return sum;
}

void acpi_init(void)
{
    // Find ACPI RDST Table Address
    rsdp_addr = find_rsdp();

    // Find ACPI MADT Table Address
    madt_addr = find_acpi_table(MADTSignature);

    // Find ACPI FADT Table Address
    fadt_addr = find_acpi_table(FADTSignature);

    // Find ACPI HPET Table Address
    hpet_addr = find_acpi_table(HPETSignature);
}
