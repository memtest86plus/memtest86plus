// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.
//
// Derived from an extract of memtest86+ smp.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// ------------------------------------------------
// smp.c - MemTest-86  Version 3.5
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "bootparams.h"
#include "efi.h"

#include "memsize.h"
#include "pmem.h"
#include "string.h"
#include "unistd.h"

#include "smp.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_APIC_IDS                256

// APIC registers

#define APICR_ID                    0x02
#define APICR_ESR                   0x28
#define APICR_ICRLO                 0x30
#define APICR_ICRHI                 0x31

// APIC destination shorthands

#define APIC_DEST_DEST              0
#define APIC_DEST_LOCAL             1
#define APIC_DEST_ALL_INC           2
#define APIC_DEST_ALL_EXC           3

// APIC IPI Command Register format

#define APIC_ICRHI_RESERVED         0x00ffffff
#define APIC_ICRHI_DEST_MASK        0xff000000
#define APIC_ICRHI_DEST_OFFSET      24

#define APIC_ICRLO_RESERVED         0xfff32000
#define APIC_ICRLO_DEST_MASK        0x000c0000
#define APIC_ICRLO_DEST_OFFSET      18
#define APIC_ICRLO_TRIGGER_MASK     0x00008000
#define APIC_ICRLO_TRIGGER_OFFSET   15
#define APIC_ICRLO_LEVEL_MASK       0x00004000
#define APIC_ICRLO_LEVEL_OFFSET     14
#define APIC_ICRLO_STATUS_MASK      0x00001000
#define APIC_ICRLO_STATUS_OFFSET    12
#define APIC_ICRLO_DESTMODE_MASK    0x00000800
#define APIC_ICRLO_DESTMODE_OFFSET  11
#define APIC_ICRLO_DELMODE_MASK     0x00000700
#define APIC_ICRLO_DELMODE_OFFSET   8
#define APIC_ICRLO_VECTOR_MASK      0x000000ff
#define APIC_ICRLO_VECTOR_OFFSET    0

// APIC trigger types

#define APIC_TRIGGER_EDGE           0
#define APIC_TRIGGER_LEVEL          1

// APIC delivery modes

#define APIC_DELMODE_FIXED          0
#define APIC_DELMODE_LOWEST         1
#define APIC_DELMODE_SMI            2
#define APIC_DELMODE_NMI            4
#define APIC_DELMODE_INIT           5
#define APIC_DELMODE_STARTUP        6
#define APIC_DELMODE_EXTINT         7

// Table signatures

#define FPSignature     ('_' | ('M' << 8) | ('P' << 16) | ('_' << 24))

#define MPCSignature    ('P' | ('C' << 8) | ('M' << 16) | ('P' << 24))

#define RSDPSignature1  ('R' | ('S' << 8) | ('D' << 16) | (' ' << 24))
#define RSDPSignature2  ('P' | ('T' << 8) | ('R' << 16) | (' ' << 24))

#define RSDTSignature   ('R' | ('S' << 8) | ('D' << 16) | ('T' << 24))

#define XSDTSignature   ('X' | ('S' << 8) | ('D' << 16) | ('T' << 24))

#define MADTSignature   ('A' | ('P' << 8) | ('I' << 16) | ('C' << 24))

// MP config table entry types

#define MP_PROCESSOR                0
#define MP_BUS                      1
#define MP_IOAPIC                   2
#define MP_INTSRC                   3
#define MP_LINTSRC                  4

// MP processor cpu_flag values

#define CPU_ENABLED                 1
#define CPU_BOOTPROCESSOR           2

// MADT processor flag values

#define MADT_PF_ENABLED             0x1
#define MADT_PF_ONLINE_CAPABLE      0x2

// Private memory heap used for AP trampoline and synchronisation objects

#define HEAP_BASE_ADDR              (smp_heap_page << PAGE_SHIFT)

#define AP_TRAMPOLINE_PAGE          (smp_heap_page)

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef uint32_t apic_register_t[4];

typedef struct {
    uint32_t    signature;      // "_MP_"
    uint32_t    phys_addr;
    uint8_t     length;
    uint8_t     spec_rev;
    uint8_t     checksum;
    uint8_t     feature[5];
} floating_pointer_struct_t;

typedef struct {
    uint32_t    signature;      // "PCMP"
    uint16_t    length;
    uint8_t     spec_rev;
    uint8_t     checksum;
    char        oem[8];
    char        product_id[12];
    uint32_t    oem_ptr;
    uint16_t    oem_size;
    uint16_t    oem_count;
    uint32_t    lapic_addr;
    uint32_t    reserved;
} mp_config_table_header_t;

typedef struct {
    uint8_t     type;           // MP_PROCESSOR
    uint8_t     apic_id;
    uint8_t     apic_ver;
    uint8_t     cpu_flag;
    uint32_t    cpu_signature;
    uint32_t    feature_flag;
    uint32_t    reserved[2];
} mp_processor_entry_t;

typedef struct {
    uint8_t     type;           // MP_BUS
    uint8_t     bus_id;
    char        bus_type[6];
} mp_bus_entry_t;

typedef struct {
    uint8_t     type;           // MP_IOAPIC
    uint8_t     apic_id;
    uint8_t     apic_ver;
    uint8_t     flags;
    uint32_t    apic_addr;
} mp_io_apic_entry_t;

typedef struct {
    uint8_t     type;
    uint8_t     irq_type;
    uint16_t    irq_flag;
    uint8_t     src_bus_id;
    uint8_t     src_bus_irq;
    uint8_t     dst_apic;
    uint8_t     dst_irq;
} mp_interrupt_entry_t;

typedef struct {
    uint8_t     type;
    uint8_t     irq_type;
    uint16_t    irq_flag;
    uint8_t     src_bus_id;
    uint8_t     src_bus_irq;
    uint8_t     dst_apic;
    uint8_t     dst_apic_lint;
} mp_local_interrupt_entry_t;

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

typedef struct {
    uint8_t     type;
    uint8_t     length;
    uint8_t     acpi_id;
    uint8_t     apic_id;
    uint32_t    flags;
} madt_processor_entry_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static const efi_guid_t EFI_ACPI_1_RDSP_GUID = { 0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };
static const efi_guid_t EFI_ACPI_2_RDSP_GUID = { 0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81} };

static volatile apic_register_t *apic = NULL;

static int8_t           apic_id_to_pcpu_num[MAX_APIC_IDS];

static uint8_t          pcpu_num_to_apic_id[MAX_PCPUS];

static volatile bool    cpu_started[MAX_PCPUS];

static uintptr_t        smp_heap_page = 0;

static uintptr_t        alloc_addr = 0;

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int num_pcpus = 1;  // There is always at least one CPU, the BSP

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static int my_apic_id(void)
{
    return (apic[APICR_ID][0]) >> 24;
}

static void apic_write(unsigned reg, uint32_t val)
{
    apic[reg][0] = val;
}

static uint32_t apic_read(unsigned reg)
{
    return apic[reg][0];
}

static void send_ipi(unsigned apic_id, unsigned trigger, unsigned level, unsigned mode, uint8_t vector)
{
    uint32_t v;

    v = apic_read(APICR_ICRHI) & 0x00ffffff;
    apic_write(APICR_ICRHI, v | (apic_id << 24));

    v = apic_read(APICR_ICRLO) & ~0xcdfff;
    v |= APIC_DEST_DEST << APIC_ICRLO_DEST_OFFSET;
    v |= trigger        << APIC_ICRLO_TRIGGER_OFFSET;
    v |= level          << APIC_ICRLO_LEVEL_OFFSET;
    v |= mode           << APIC_ICRLO_DELMODE_OFFSET;
    v |= vector;
    apic_write(APICR_ICRLO, v);
}

static int checksum(const void *data, int length)
{
    uint8_t sum = 0;

    uint8_t *ptr = (uint8_t *)data;
    while (length--) {
        sum += *ptr++;
    }
    return sum;
}

static floating_pointer_struct_t *scan_for_floating_ptr_struct(uintptr_t addr, int length)
{
    uint32_t *ptr = (uint32_t *)addr;
    uint32_t *end = ptr + length / sizeof(uint32_t);

    while (ptr < end) {
        if (*ptr == FPSignature && checksum(ptr, 16) == 0) {
            floating_pointer_struct_t *fp = (floating_pointer_struct_t *)ptr;
            if (fp->length == 1 && (fp->spec_rev == 1 || fp->spec_rev == 4)) {
                return fp;
            }
        }
        ptr++;
    }
    return NULL;
}

static bool read_mp_config_table(uintptr_t addr)
{
    mp_config_table_header_t *mpc = (mp_config_table_header_t *)addr;

    if (mpc->signature != MPCSignature || checksum(mpc, mpc->length) != 0) {
        return false;
    }

    apic = (volatile apic_register_t *)((uintptr_t)mpc->lapic_addr);

    uint8_t *tab_entry_ptr = (uint8_t *)mpc + sizeof(mp_config_table_header_t);
    uint8_t *mpc_table_end = (uint8_t *)mpc + mpc->length;

    while (tab_entry_ptr < mpc_table_end) {
        switch (*tab_entry_ptr) {
          case MP_PROCESSOR: {
            mp_processor_entry_t *entry = (mp_processor_entry_t *)tab_entry_ptr;

            if (entry->cpu_flag & CPU_BOOTPROCESSOR) {
                // BSP is CPU 0
                pcpu_num_to_apic_id[0] = entry->apic_id;
            } else if (num_pcpus < MAX_PCPUS) {
                pcpu_num_to_apic_id[num_pcpus] = entry->apic_id;
                num_pcpus++;
            }

            // we cannot handle non-local 82489DX apics
            if ((entry->apic_ver & 0xf0) != 0x10) {
                num_pcpus = 1;   // reset to initial value
                return false;
            }

            tab_entry_ptr += sizeof(mp_processor_entry_t);
            break;
          }
          case MP_BUS: {
            tab_entry_ptr += sizeof(mp_bus_entry_t);
            break;
          }
          case MP_IOAPIC: {
            tab_entry_ptr += sizeof(mp_io_apic_entry_t);
            break;
          }
          case MP_INTSRC:
            tab_entry_ptr += sizeof(mp_interrupt_entry_t);
            break;
          case MP_LINTSRC:
            tab_entry_ptr += sizeof(mp_local_interrupt_entry_t);
            break;
        default:
            num_pcpus = 1;   // reset to initial value
            return false;
        }
    }
    return true;
}

static bool find_cpus_in_floating_mp_struct(void)
{
    // Search for the Floating MP structure pointer.
    floating_pointer_struct_t *fp = scan_for_floating_ptr_struct(0x0, 0x400);
    if (fp == NULL) {
        fp = scan_for_floating_ptr_struct(639*0x400, 0x400);
    }
    if (fp == NULL) {
        fp = scan_for_floating_ptr_struct(0xf0000, 0x10000);
    }
    if (fp == NULL) {
        // Search the BIOS EBDA area.
        uintptr_t address = *(uint16_t *)0x40E << 4;
        if (address) {
            fp = scan_for_floating_ptr_struct(address, 0x400);
        }
    }
    if (fp == NULL) {
        // Floating MP structure pointer not found - give up.
        return false;
    }

    if (fp->feature[0] > 0 && fp->feature[0] <= 7) {
        // This is a default config, so plug in the numbers.
        apic = (volatile apic_register_t *)0xFEE00000;
        pcpu_num_to_apic_id[0] = 0;
        pcpu_num_to_apic_id[1] = 1;
        num_pcpus = 2;
        return true;
    }

    // Do we have a pointer to a MP configuration table?
    if (fp->phys_addr != 0) {
        if (read_mp_config_table(fp->phys_addr)) {
            // Found a good MP table, done.
            return true;
        }
    }

    return false;
}

static rsdp_t *scan_for_rsdp(uintptr_t addr, int length)
{
    uint32_t *ptr = (uint32_t *)addr;
    uint32_t *end = ptr + length / sizeof(uint32_t);

    while (ptr < end) {
        rsdp_t *rp = (rsdp_t *)ptr;
        if (*ptr == RSDPSignature1 && *(ptr+1) == RSDPSignature2 && checksum(ptr, 20) == 0) {
            if (rp->revision < 2 || (rp->length < 1024 && checksum(ptr, rp->length) == 0)) {
                return rp;
            }
        }
        ptr += 4;
    }
    return NULL;
}

static bool parse_madt(void *addr)
{
    mp_config_table_header_t *mpc = (mp_config_table_header_t *)addr;

    if (checksum(mpc, mpc->length) != 0) {
        return false;
    }

    apic = (volatile apic_register_t *)((uintptr_t)mpc->lapic_addr);

    int found_cpus = 0;

    uint8_t *tab_entry_ptr = (uint8_t *)mpc + sizeof(mp_config_table_header_t);
    uint8_t *mpc_table_end = (uint8_t *)mpc + mpc->length;
    while (tab_entry_ptr < mpc_table_end) {
        madt_processor_entry_t *entry = (madt_processor_entry_t *)tab_entry_ptr;
        if (entry->type == MP_PROCESSOR) {
            if (entry->flags & (MADT_PF_ENABLED|MADT_PF_ONLINE_CAPABLE)) {
                if (num_pcpus < MAX_PCPUS) {
                    pcpu_num_to_apic_id[found_cpus] = entry->apic_id;
                    // The first CPU is the BSP, don't increment.
                    if (found_cpus > 0) {
                        num_pcpus++;
                    }
                }
                found_cpus++;
            }
        }
        tab_entry_ptr += entry->length;
    }
    return true;
}

static rsdp_t *find_rsdp_in_efi32_system_table(efi32_system_table_t *system_table)
{
    efi32_config_table_t *config_tables = (efi32_config_table_t *)((uintptr_t)system_table->config_tables);

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

#ifdef __x86_64__
static rsdp_t *find_rsdp_in_efi64_system_table(efi64_system_table_t *system_table)
{
    efi64_config_table_t *config_tables = (efi64_config_table_t *)((uintptr_t)system_table->config_tables);

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

static bool find_cpus_in_rsdp(void)
{
    const boot_params_t *boot_params = (boot_params_t *)boot_params_addr;

    const efi_info_t *efi_info = &boot_params->efi_info;

    // Search for the RSDP
    rsdp_t *rp = NULL;
    if (boot_params->acpi_rsdp_addr != 0) {
        // Validate it
        rp = scan_for_rsdp(boot_params->acpi_rsdp_addr, 0x8);
    }
    if (rp == NULL && efi_info->loader_signature == EFI32_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = (uintptr_t)efi_info->sys_tab;
        rp = find_rsdp_in_efi32_system_table((efi32_system_table_t *)system_table_addr);
    }
#ifdef __x86_64__
    if (rp == NULL && efi_info->loader_signature == EFI64_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = (uintptr_t)efi_info->sys_tab_hi << 32 | (uintptr_t)efi_info->sys_tab;
        rp = find_rsdp_in_efi64_system_table((efi64_system_table_t *)system_table_addr);
    }
#endif
    if (rp == NULL) {
        // Search the BIOS EBDA area.
        uintptr_t address = *(uint16_t *)0x40E << 4;
        if (address) {
            rp = scan_for_rsdp(address, 0x400);
        }
    }
    if (rp == NULL) {
        // Search the BIOS reserved area.
        rp = scan_for_rsdp(0xE0000, 0x20000);
    }
    if (rp == NULL) {
        // RSDP not found, give up.
        return false;
    }

    // Found the RSDP, now get either the RSDT or XSDT and scan it for a pointer to the MADT.
    rsdt_header_t *rt;
    if (rp->revision >= 2) {
        rt = (rsdt_header_t *)((uintptr_t)rp->xsdt_addr);
        if (rt == 0) {
            return false;
        }
        // Validate the XSDT.
        if (*(uint32_t *)rt != XSDTSignature) {
            return false;
        }
        if (checksum(rt, rt->length) != 0) {
            return false;
        }
        // Scan the XSDT for a pointer to the MADT.
        uint64_t *tab_ptr = (uint64_t *)((uint8_t *)rt + sizeof(rsdt_header_t));
        uint64_t *tab_end = (uint64_t *)((uint8_t *)rt + rt->length);

        while (tab_ptr < tab_end) {
            uint32_t *ptr = (uint32_t *)((uintptr_t)(*tab_ptr++));  // read the next table entry

            if (ptr && *ptr == MADTSignature) {
                if (parse_madt(ptr)) {
                    return true;
                }
            }
        }
    } else {
        rt = (rsdt_header_t *)((uintptr_t)rp->rsdt_addr);
        if (rt == 0) {
            return false;
        }
        // Validate the RSDT.
        if (*(uint32_t *)rt != RSDTSignature) {
            return false;
        }
        if (checksum(rt, rt->length) != 0) {
            return false;
        }
        // Scan the RSDT for a pointer to the MADT.
        uint32_t *tab_ptr = (uint32_t *)((uint8_t *)rt + sizeof(rsdt_header_t));
        uint32_t *tab_end = (uint32_t *)((uint8_t *)rt + rt->length);

        while (tab_ptr < tab_end) {
            uint32_t *ptr = (uint32_t *)((uintptr_t)(*tab_ptr++));  // read the next table entry

            if (ptr && *ptr == MADTSignature) {
                if (parse_madt(ptr)) {
                    return true;
                }
            }
        }
    }

    return false;
}

static smp_error_t start_cpu(int pcpu_num)
{
    int apic_id = pcpu_num_to_apic_id[pcpu_num];

    // Clear the APIC ESR register.
    apic_write(APICR_ESR, 0);
    apic_read(APICR_ESR);

    // Pulse the INIT IPI.
    send_ipi(apic_id, APIC_TRIGGER_LEVEL, 1, APIC_DELMODE_INIT, 0);
    usleep(100000);
    send_ipi(apic_id, APIC_TRIGGER_LEVEL, 0, APIC_DELMODE_INIT, 0);

    for (int num_sipi = 0; num_sipi < 2; num_sipi++) {
        apic_write(APICR_ESR, 0);

        send_ipi(apic_id, 0, 0, APIC_DELMODE_STARTUP, AP_TRAMPOLINE_PAGE);

        bool send_pending;
        int timeout = 0;
        do {
            usleep(10);
            timeout++;
            send_pending = (apic_read(APICR_ICRLO) & APIC_ICRLO_STATUS_MASK) != 0;
        } while (send_pending && timeout < 1000);

        if (send_pending) {
            return SMP_ERR_STARTUP_IPI_NOT_SENT;
        }

        usleep(100000);

        uint32_t error = apic_read(APICR_ESR) & 0xef;
        if (error) {
            return SMP_ERR_STARTUP_IPI_ERROR + error;
        }
    }

    int timeout = 0;
    do {
        usleep(10);
        timeout++;
    } while (!cpu_started[pcpu_num] && timeout < 100000);

    if (!cpu_started[pcpu_num]) {
        return SMP_ERR_BOOT_TIMEOUT;
    }

    return SMP_ERR_NONE;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void smp_init(bool smp_enable)
{
    for (int i = 0; i < MAX_APIC_IDS; i++) {
        apic_id_to_pcpu_num[i] = 0;
    }

    for (int i = 0; i < MAX_PCPUS; i++) {
        pcpu_num_to_apic_id[i]  = 0;
        cpu_started[i] = false;
    }

    num_pcpus = 1;

    if (smp_enable) {
        (void)(find_cpus_in_rsdp() || find_cpus_in_floating_mp_struct());
    }

    for (int i = 0; i < num_pcpus; i++) {
        apic_id_to_pcpu_num[pcpu_num_to_apic_id[i]] = i;
    }

    // Reserve last page of first segment for AP trampoline and sync objects.
    // These need to remain pinned in place during relocation.
    smp_heap_page = --pm_map[0].end;

    ap_startup_addr = (uintptr_t)startup;

    size_t ap_trampoline_size = ap_trampoline_end - ap_trampoline;
    memcpy((uint8_t *)HEAP_BASE_ADDR, ap_trampoline, ap_trampoline_size);

    alloc_addr = HEAP_BASE_ADDR + ap_trampoline_size;
}

smp_error_t smp_start(bool enable_pcpu[MAX_PCPUS])
{
    enable_pcpu[0] = true;  // we don't support disabling the boot CPU

    for (int i = 1; i < num_pcpus; i++) {
        if (enable_pcpu[i]) {
            smp_error_t error = start_cpu(i);
            if (error != SMP_ERR_NONE) {
                return error;
            }
        }
    }

    return SMP_ERR_NONE;
}

void smp_set_ap_booted(int pcpu_num)
{
    cpu_started[pcpu_num] = true;
}

int smp_my_pcpu_num(void)
{
    return num_pcpus > 1 ? apic_id_to_pcpu_num[my_apic_id()] : 0;
}

barrier_t *smp_alloc_barrier(int num_threads)
{
    barrier_t *barrier = (barrier_t  *)(alloc_addr);
    alloc_addr += sizeof(barrier_t);
    barrier_init(barrier, num_threads);
    return barrier;
}

spinlock_t *smp_alloc_mutex()
{
    spinlock_t *mutex = (spinlock_t *)(alloc_addr);
    alloc_addr += sizeof(spinlock_t);
    spin_unlock(mutex);
    return mutex;
}
