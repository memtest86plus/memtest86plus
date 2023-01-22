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

#define RSDTSignature   ('R' | ('S' << 8) | ('D' << 16) | ('T' << 24)) // Root System Description Table

#define XSDTSignature   ('X' | ('S' << 8) | ('D' << 16) | ('T' << 24)) // Extended System Description Table

#define MADTSignature   ('A' | ('P' << 8) | ('I' << 16) | ('C' << 24)) // Multiple APIC Description Table

#define FADTSignature   ('F' | ('A' << 8) | ('C' << 16) | ('P' << 24)) // Fixed ACPI Description Table

#define HPETSignature   ('H' | ('P' << 8) | ('E' << 16) | ('T' << 24)) // High Precision Event Timer

#define EINJSignature   ('E' | ('I' << 8) | ('N' << 16) | ('J' << 24)) // Error Injection Table
#define ERSTSignature   ('E' | ('R' << 8) | ('S' << 16) | ('T' << 24)) // Error Record Serialization Table
#define CPEPSignature   ('C' | ('P' << 8) | ('E' << 16) | ('P' << 24)) // Corrected Platform Error Polling Table
#define HESTSignature   ('H' | ('E' << 8) | ('S' << 16) | ('T' << 24)) // Hardware Error Source Table

#define SLITSignature   ('S' | ('L' << 8) | ('I' << 16) | ('T' << 24)) // System Locality Information Table (NUMA)
#define SRATSignature   ('S' | ('R' << 8) | ('A' << 16) | ('T' << 24)) // System Resource Affinity Table (NUMA)

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct __attribute__ ((packed)) {
    uint8_t     address_space;
    uint8_t     bit_width;
    uint8_t     bit_offset;
    uint8_t     access_size;
    uint64_t    address;
} acpi_gen_addr_struct;

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

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static const efi_guid_t EFI_ACPI_1_RDSP_GUID = { 0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };
static const efi_guid_t EFI_ACPI_2_RDSP_GUID = { 0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81} };

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

const char *rsdp_source = "";

acpi_t acpi_config = {0, 0, 0, 0, 0, 0, 0, 0, 0, false};

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
   rsdp_t *rp = (rsdp_t *)acpi_config.rsdp_addr;

    // Found the RSDP, now get either the RSDT or XSDT
    // and scan it for a pointer to the table we're looking for
    rsdt_header_t *rt;

    if (acpi_config.ver_maj < rp->revision) {
        acpi_config.ver_maj = rp->revision;
    }

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
        // Scan the XSDT for a pointer to the table we're looking for.
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
        // Scan the RSDT for a pointer to the table we're looking for.
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

static bool parse_fadt(uintptr_t fadt_addr)
{
    // FADT is a very big & complex table and we only need a few pieces of data.
    // We use byte offset instead of a complete struct.

    // FADT Header is identical to RSDP Header
    rsdt_header_t *fadt = (rsdt_header_t *)fadt_addr;

    // Validate FADT
    if (fadt == NULL || acpi_checksum(fadt, fadt->length) != 0) {
        return false;
    }

    // Get ACPI Version
    acpi_config.ver_maj = fadt->revision;

    if (fadt->length > FADT_MINOR_REV_OFFSET) {
        acpi_config.ver_min = *(uint8_t *)(fadt_addr+FADT_MINOR_REV_OFFSET) & 0xF;
    }

    // Get Old PM Base Address (32-bit IO)
    acpi_config.pm_addr  = *(uint32_t *)(fadt_addr+FADT_PM_TMR_BLK_OFFSET);
    acpi_config.pm_is_io = true;

#ifdef __x86_64__
    acpi_gen_addr_struct *rt;

    // Get APIC Timer Address
    if (fadt->length > FADT_X_PM_TMR_BLK_OFFSET) {
        rt = (acpi_gen_addr_struct *)map_region(fadt_addr+FADT_X_PM_TMR_BLK_OFFSET, sizeof(acpi_gen_addr_struct), true);

        acpi_config.pm_is_io = (rt->address_space == 1) ? true : false;

        if (rt->address != 0) {
            acpi_config.pm_addr = rt->address;
        }
    }
#endif

    return true;
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
    acpi_config.rsdp_addr = find_rsdp();

    if (acpi_config.rsdp_addr == 0) {
        return;
    }

    acpi_config.madt_addr = find_acpi_table(MADTSignature);

    acpi_config.fadt_addr = find_acpi_table(FADTSignature);

    if (acpi_config.fadt_addr) {
        parse_fadt(acpi_config.fadt_addr);
    }

    acpi_config.hpet_addr = find_acpi_table(HPETSignature);

    acpi_config.srat_addr = find_acpi_table(SRATSignature);

    acpi_config.slit_addr = find_acpi_table(SLITSignature);
}
