// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// Derived from an extract of memtest86+ smp.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// ------------------------------------------------
// smp.c - MemTest-86  Version 3.5
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#if defined(__loongarch_lp64)
#include "registers.h"
#include <larchintrin.h>
#endif

#if defined(__aarch64__)
#include "registers.h"
#include "cache.h"
#include "psci.h"
#endif

#include "acpi.h"
#include "boot.h"
#include "macros.h"
#include "bootparams.h"
#include "efi.h"

#include "cpuid.h"
#include "heap.h"
#include "hwquirks.h"
#include "memrw.h"
#include "memsize.h"
#include "msr.h"
#include "string.h"
#include "unistd.h"
#include "vmem.h"
#include "pmem.h"

#include "smp.h"

#define SEQUENTIAL_AP_START         0

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define APIC_REGS_SIZE              SIZE_C(4,KB)

// APIC registers

#define APIC_REG_ID                 0x02
#define APIC_REG_VER                0x03
#define APIC_REG_ESR                0x28
#define APIC_REG_ICRLO              0x30
#define APIC_REG_ICRHI              0x31

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

// APIC ICR busy flag

#define	APIC_ICR_BUSY               (1 << 12)

// IA32_APIC_BASE MSR bits

#define IA32_APIC_ENABLED           (1 << 11)
#define IA32_APIC_EXTENDED          (1 << 10)

// Table signatures

#define FPSignature     ('_' | ('M' << 8) | ('P' << 16) | ('_' << 24))
#define MPCSignature    ('P' | ('C' << 8) | ('M' << 16) | ('P' << 24))

// MP config table entry types

#define MP_PROCESSOR                   0
#define MP_BUS                         1
#define MP_IOAPIC                      2
#define MP_INTSRC                      3
#define MP_LINTSRC                     4

// MP processor cpu_flag values

#define CPU_ENABLED                    1
#define CPU_BOOTPROCESSOR              2

// MADT entry types

#define MADT_PROCESSOR                 0
#define MADT_LAPIC_ADDR                5
#define MADT_GICC                      11
#define MADT_CORE_PIC                  17

#define MADT_PROCESSOR_X2APIC          9

// MADT processor flag values

#define MADT_PF_ENABLED                0x1
#define MADT_PF_ONLINE_CAPABLE         0x2

// MADT GICC flag values

#define MADT_GICC_ENABLED              0x1
#define MADT_GICC_ONLINE_CAPABLE       0x8

// SRAT entry types

#define SRAT_PROCESSOR_APIC_AFFINITY   0
#define SRAT_MEMORY_AFFINITY           1
#define SRAT_PROCESSOR_X2APIC_AFFINITY 2
#define SRAT_PROCESSOR_GICC_AFFINITY   3

// SRAT flag values
#define SRAT_PAAF_ENABLED              1
#define SRAT_MAF_ENABLED               1
#define SRAT_PXAAF_ENABLED             1
#define SRAT_GICC_AAF_ENABLED          1

// Private memory heap used for AP trampoline and synchronisation objects

#define HEAP_BASE_ADDR              (smp_heap_page << PAGE_SHIFT)

// The pinned synchronization arena is two pages: the first page holds the
// AP trampoline on x86, the rest is carved up for barrier objects, mutexes
// and future synchronization state. Both pages are excluded from pm_map
// testing by the heap allocator.

#define SYNC_ARENA_PAGES            2
#define SYNC_ARENA_LIMIT            (HEAP_BASE_ADDR + SYNC_ARENA_PAGES * PAGE_SIZE)

#define AP_TRAMPOLINE_PAGE          (smp_heap_page)

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef volatile uint32_t apic_register_t[4];

// The type used to identify a CPU core. On most architectures this is the
// local APIC ID or equivalent. On ARM64 it is the (up to 40-bit) MPIDR
// affinity value.

#if defined(__aarch64__)
typedef uint64_t cpu_apic_id_t;
#else
typedef uint32_t cpu_apic_id_t;
#endif

typedef struct __attribute__((packed)) {
    uint32_t proximity_domain_idx;
    uint64_t start;
    uint64_t end;
} memory_affinity_t;

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
    rsdt_header_t h;
    uint32_t    lapic_addr;
    uint32_t    flags;
} madt_table_header_t;

typedef struct {
    uint8_t     type;
    uint8_t     length;
} madt_entry_header_t;

#if defined(__i386__) || defined(__x86_64__)

typedef struct {
    uint8_t     type;
    uint8_t     length;
    uint8_t     acpi_id;
    uint8_t     apic_id;
    uint32_t    flags;
} madt_processor_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t     type;
    uint8_t     length;
    uint16_t    reserved;
    uint32_t    apic_id;
    uint32_t    flags;
    uint32_t    acpi_id;
} madt_processor_x2apic_entry_t;

#elif defined(__loongarch_lp64)

#pragma pack(1)
typedef struct {
    uint8_t     type;
    uint8_t     length;
    uint8_t     version;
    uint32_t    processor_id;
    uint32_t    core_id;
    uint32_t    flags;
} madt_processor_entry_t;
#pragma pack ()

#elif defined(__aarch64__)

typedef struct __attribute__((packed)) {
    uint8_t     type;
    uint8_t     length;
    uint16_t    reserved1;
    uint32_t    cpu_interface_num;
    uint32_t    acpi_processor_uid;
    uint32_t    flags;
    uint32_t    parking_version;
    uint32_t    performance_gsiv;
    uint64_t    parked_address;
    uint64_t    gicc_base;
    uint64_t    gicv_base;
    uint64_t    gich_base;
    uint32_t    vgic_gsiv;
    uint64_t    gicr_base;
    uint64_t    mpidr;
} madt_gicc_entry_t;

#endif

typedef struct {
    uint8_t     type;
    uint8_t     length;
    uint16_t    reserved;
    uint64_t    lapic_addr;
} madt_lapic_addr_entry_t;


typedef struct {
    rsdt_header_t h;
    uint32_t    revision;
    uint64_t    reserved;
} srat_table_header_t;

typedef struct {
    uint8_t     type;
    uint8_t     length;
} srat_entry_header_t;

// SRAT subtable type 00: Processor Local APIC/SAPIC Affinity.
typedef struct __attribute__((packed)) {
    uint8_t         type;
    uint8_t         length;
    uint8_t         proximity_domain_low;
    uint8_t         apic_id;
    uint32_t        flags;
    struct {
        uint32_t    local_sapic_eid       : 8;
        uint32_t    proximity_domain_high : 24;
    };
    uint32_t        clock_domain;
} srat_processor_lapic_affinity_entry_t;

// SRAT subtable type 01: Memory Affinity.
typedef struct __attribute__ ((packed)) {
    uint8_t     type;
    uint8_t     length;
    uint32_t    proximity_domain;
    uint16_t    reserved1;
    uint64_t    base_address;
    uint64_t    address_length;
    uint32_t    reserved2;
    uint32_t    flags;
    uint64_t    reserved3;
} srat_memory_affinity_entry_t;

// SRAT subtable type 02: Processor Local x2APIC Affinity
typedef struct __attribute__((packed)) {
    uint8_t         type;
    uint8_t         length;
    uint16_t        reserved1;
    uint32_t        proximity_domain;
    uint32_t        apic_id;
    uint32_t        flags;
    uint32_t        clock_domain;
    uint32_t        reserved2;
} srat_processor_lx2apic_affinity_entry_t;

// SRAT subtable type 03: Processor Local GICC Affinity (AArch64).
// The CPU is identified by its ACPI Processor UID, not by the MPIDR.
typedef struct __attribute__((packed)) {
    uint8_t         type;
    uint8_t         length;
    uint32_t        proximity_domain;
    uint32_t        acpi_processor_uid;
    uint32_t        flags;
    uint32_t        clock_domain;
} srat_processor_gicc_affinity_entry_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

#if !defined(__aarch64__)
static apic_register_t   *apic = NULL;
#endif

#if VMEM_MAX_CONTEXTS > 1
static uint32_t          cpu_num_to_proximity_domain_idx[MAX_CPUS];
#endif

static cpu_apic_id_t     cpu_num_to_apic_id[MAX_CPUS];

#if defined(__aarch64__)
// The ACPI Processor UID of each CPU, retained alongside the MPIDR because
// SRAT type 3 (GICC affinity) identifies CPUs by UID rather than by MPIDR.
static uint32_t          cpu_num_to_acpi_uid[MAX_CPUS];

// Whether the cpu_num_to_acpi_uid slot has been assigned; UID 0 is a
// legitimate ACPI Processor UID and must not be mistaken for unassigned.
static bool              cpu_num_to_uid_assigned[MAX_CPUS];
#endif

// Whether cpu_num_to_proximity_domain_idx[] has been assigned yet in the
// current SRAT pass; domain index 0 is a valid assignment, so a separate
// marker is required to detect conflicting affinities.
#if VMEM_MAX_CONTEXTS > 1
static bool              cpu_num_to_domain_assigned[MAX_CPUS];

static memory_affinity_t memory_affinity_ranges[MAX_APIC_IDS];

static uint32_t          proximity_domains[MAX_PROXIMITY_DOMAINS];
#endif

uint16_t                  used_cpus_in_proximity_domain[MAX_PROXIMITY_DOMAINS];

static uintptr_t         smp_heap_page = 0;

static uintptr_t         alloc_addr = 0;

// Fallback mutex used if the pinned synchronization arena is exhausted; a
// shared global lock remains correct, it only serializes more broadly.
static spinlock_t        fallback_mutex = 0;

#if !defined(__aarch64__)
static bool              apic_x2apic = false;
#endif

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int     num_available_cpus = 1;  // There is always at least one CPU, the BSP
#if VMEM_MAX_CONTEXTS > 1
int     num_memory_affinity_ranges = 0;
int     num_proximity_domains = 0;
#endif
bool    map_numa_memory_range = false;
uint8_t highest_map_bit = 0;

// Set when the SRAT declares more distinct proximity domains than
// MAX_PROXIMITY_DOMAINS; NUMA placement is then disabled entirely.
#if VMEM_MAX_CONTEXTS > 1
bool    smp_topology_too_large = false;
#endif

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

#if !defined(__aarch64__)
static int my_apic_id(void)
{
#if defined(__i386__) || defined(__x86_64__)
    if (apic_x2apic) {
        uint32_t msrl, msrh;
        rdmsr(MSR_IA32_X2APIC_BASE + APIC_REG_ID, msrl, msrh);
        return (int)msrl;
    }
    return read32(&apic[APIC_REG_ID][0]) >> 24;
#elif defined(__loongarch_lp64)
    return ((int)__csrrd_w(0x20));
#endif
}
#endif

#if defined(__i386__) || defined(__x86_64__)
static void apic_write(int reg, uint32_t val)
{
    if (apic_x2apic) {
        if (reg == APIC_REG_ICRHI) {
            return;
        }
        wrmsr(MSR_IA32_X2APIC_BASE + reg, val, 0);
        return;
    }
    write32(&apic[reg][0], val);
}

static uint32_t apic_read(int reg)
{
    if (apic_x2apic) {
        uint32_t msrl, msrh;
        if (reg == APIC_REG_ICRHI || reg == APIC_REG_ICRLO) {
            rdmsr(MSR_IA32_X2APIC_BASE + APIC_REG_ICRLO, msrl, msrh);
            return reg == APIC_REG_ICRHI ? msrh : msrl;
        }
        rdmsr(MSR_IA32_X2APIC_BASE + reg, msrl, msrh);
        return msrl;
    }
    return read32(&apic[reg][0]);
}

static floating_pointer_struct_t *scan_for_floating_ptr_struct(uintptr_t addr, int length)
{
    uint32_t *ptr = (uint32_t *)addr;
    uint32_t *end = ptr + length / sizeof(uint32_t);

    while (ptr < end) {
        if (*ptr == FPSignature && acpi_checksum(ptr, 16) == 0) {
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
    if (apic_x2apic) return false;

    mp_config_table_header_t *mpc = (mp_config_table_header_t *)map_region(addr, sizeof(mp_config_table_header_t), true);
    if (mpc == NULL) return false;

    mpc = (mp_config_table_header_t *)map_region(addr, mpc->length, true);
    if (mpc == NULL) return false;

    if (mpc->signature != MPCSignature || acpi_checksum(mpc, mpc->length) != 0) {
        return false;
    }

    apic = (volatile apic_register_t *)map_region(mpc->lapic_addr, APIC_REGS_SIZE, false);
    if (apic == NULL) return false;

    uint8_t *tab_entry_ptr = (uint8_t *)mpc + sizeof(mp_config_table_header_t);
    uint8_t *mpc_table_end = (uint8_t *)mpc + mpc->length;

    while (tab_entry_ptr < mpc_table_end) {
        switch (*tab_entry_ptr) {
          case MP_PROCESSOR: {
            mp_processor_entry_t *entry = (mp_processor_entry_t *)tab_entry_ptr;

            if (entry->cpu_flag & CPU_BOOTPROCESSOR) {
                // BSP is CPU 0
                cpu_num_to_apic_id[0] = entry->apic_id;
            } else if (num_available_cpus < MAX_CPUS) {
                cpu_num_to_apic_id[num_available_cpus] = entry->apic_id;
                num_available_cpus++;
            }

            // we cannot handle non-local 82489DX apics
            if ((entry->apic_ver & 0xf0) != 0x10) {
                num_available_cpus = 1;   // reset to initial value
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
            num_available_cpus = 1;   // reset to initial value
            return false;
        }
    }
    return true;
}

static bool find_cpus_in_floating_mp_struct(void)
{
    if (apic_x2apic) return false;

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
        uintptr_t address = (uintptr_t)bda_read16(0x40E) << 4;
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
        apic = (volatile apic_register_t *)map_region(0xFEE00000, APIC_REGS_SIZE, false);
        if (apic == NULL) return false;
        cpu_num_to_apic_id[0] = 0;
        cpu_num_to_apic_id[1] = 1;
        num_available_cpus = 2;
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
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__loongarch_lp64)
// Firmware may list the same core twice (e.g. as both a local APIC and an
// x2APIC entry on x86, or with a duplicated core ID on LoongArch). Reject
// duplicates so the SRAT affinity mapping stays unambiguous.
static bool apic_id_already_listed(cpu_apic_id_t apic_id, int found_cpus)
{
    int count = found_cpus < MAX_CPUS ? found_cpus : MAX_CPUS;

    for (int i = 0; i < count; i++) {
        if (cpu_num_to_apic_id[i] == apic_id) {
            return true;
        }
    }
    return false;
}
#endif

static bool find_cpus_in_madt(void)
{
    if (acpi_config.madt_addr == 0) {
        return false;
    }

    madt_table_header_t *mpc = (madt_table_header_t *)map_region(acpi_config.madt_addr, sizeof(madt_table_header_t), true);
    if (mpc == NULL) return false;

    mpc = (madt_table_header_t *)map_region(acpi_config.madt_addr, mpc->h.length, true);
    if (mpc == NULL) return false;

    if (acpi_checksum(mpc, mpc->h.length) != 0) {
        return false;
    }

    uintptr_t apic_addr = mpc->lapic_addr;

    int found_cpus = 0;

#if defined(__aarch64__)
    uint64_t bsp_mpidr = read_sysreg(mpidr_el1) & MPIDR_AFFINITY_MASK;
    cpu_num_to_apic_id[0] = bsp_mpidr;
#endif

    uint8_t *tab_entry_ptr = (uint8_t *)mpc + sizeof(*mpc);
    uint8_t *mpc_table_end = (uint8_t *)mpc + mpc->h.length;
    while (tab_entry_ptr + sizeof(madt_entry_header_t) <= mpc_table_end) {
        madt_entry_header_t *entry_header = (madt_entry_header_t *)tab_entry_ptr;
        // Reject malformed entries that could make us read past the end of
        // the table or loop forever.
        if (entry_header->length < sizeof(madt_entry_header_t)
         || tab_entry_ptr + entry_header->length > mpc_table_end) {
            return false;
        }
#if defined(__i386__) || defined(__x86_64__)
        if (entry_header->type == MADT_PROCESSOR) {
            if (entry_header->length != sizeof(madt_processor_entry_t)) {
                return false;
            }
            madt_processor_entry_t *entry = (madt_processor_entry_t *)tab_entry_ptr;
            if ((entry->flags & (MADT_PF_ENABLED|MADT_PF_ONLINE_CAPABLE))
             && !apic_id_already_listed(entry->apic_id, found_cpus)) {
                if (num_available_cpus < MAX_CPUS) {
                    cpu_num_to_apic_id[found_cpus] = entry->apic_id;
                    // The first CPU is the BSP, don't increment.
                    if (found_cpus > 0) {
                        num_available_cpus++;
                    }
                }
                found_cpus++;
            }
        }
        else if (entry_header->type == MADT_PROCESSOR_X2APIC) {
            if (entry_header->length != sizeof(madt_processor_x2apic_entry_t)) {
                return false;
            }
            madt_processor_x2apic_entry_t *entry = (madt_processor_x2apic_entry_t *)tab_entry_ptr;
            if ((entry->flags & MADT_PF_ENABLED)
             && !apic_id_already_listed(entry->apic_id, found_cpus)) {
                if (num_available_cpus < MAX_CPUS) {
                    cpu_num_to_apic_id[found_cpus] = entry->apic_id;
                    // The first CPU is the BSP, don't increment.
                    if (found_cpus > 0) {
                        num_available_cpus++;
                    }
                }
                found_cpus++;
            }
        }
        else if (entry_header->type == MADT_LAPIC_ADDR) {
            if (entry_header->length != sizeof(madt_lapic_addr_entry_t)) {
                return false;
            }
            madt_lapic_addr_entry_t *entry = (madt_lapic_addr_entry_t *)tab_entry_ptr;
            apic_addr = (uintptr_t)entry->lapic_addr;
        }
#elif defined(__loongarch_lp64)
        if (entry_header->type == MADT_CORE_PIC) {
            madt_processor_entry_t *entry = (madt_processor_entry_t *)tab_entry_ptr;
            if (entry->flags & (MADT_PF_ENABLED|MADT_PF_ONLINE_CAPABLE)) {
                // Reject duplicate core IDs: the SRAT affinity mapping would
                // be ambiguous.
                if (apic_id_already_listed(entry->core_id, found_cpus)) {
                    // Skip the duplicate: aborting here would silently drop
                    // every later CPU from the enumeration.
                    tab_entry_ptr += entry_header->length;
                    continue;
                }
                if (num_available_cpus < MAX_CPUS) {
                    cpu_num_to_apic_id[found_cpus] = entry->core_id;
                    // The first CPU is the BSP, don't increment.
                    if (found_cpus > 0) {
                        num_available_cpus++;
                    }
                }
                found_cpus++;
            }
        }
#elif defined(__aarch64__)
        if (entry_header->type == MADT_GICC) {
            // GICC entries are 76 bytes or longer, depending on the ACPI
            // revision. All variants have the MPIDR at the same offset.
            if (entry_header->length < 76) {
                return false;
            }
            madt_gicc_entry_t *entry = (madt_gicc_entry_t *)tab_entry_ptr;
            if (entry->flags & (MADT_GICC_ENABLED|MADT_GICC_ONLINE_CAPABLE)) {
                uint64_t mpidr = entry->mpidr & MPIDR_AFFINITY_MASK;
                uint32_t uid = entry->acpi_processor_uid;
                // Reject duplicate Processor UIDs: SRAT type 3 resolves CPUs
                // by UID, so a duplicated UID would make the mapping ambiguous.
                bool uid_already_listed = false;
                for (int i = 0; i < num_available_cpus; i++) {
                    if (cpu_num_to_uid_assigned[i] && cpu_num_to_acpi_uid[i] == uid) {
                        uid_already_listed = true;
                        break;
                    }
                }
                if (uid_already_listed) {
                    // Skip the duplicate: aborting the walk here would
                    // silently drop every later CPU from the enumeration.
                    tab_entry_ptr += entry_header->length;
                    continue;
                }
                if (mpidr == bsp_mpidr) {
                    cpu_num_to_acpi_uid[0] = uid;
                    cpu_num_to_uid_assigned[0] = true;
                } else if (num_available_cpus < MAX_CPUS) {
                    cpu_num_to_apic_id[num_available_cpus] = mpidr;
                    cpu_num_to_acpi_uid[num_available_cpus] = uid;
                    cpu_num_to_uid_assigned[num_available_cpus] = true;
                    num_available_cpus++;
                }
                found_cpus++;
            }
        }
#endif
        tab_entry_ptr += entry_header->length;
    }

#if defined(__aarch64__)
    // There is no memory-mapped local interrupt controller to map.
    (void)apic_addr;
    (void)found_cpus;
#else
    if (!apic_x2apic) {
        apic = (volatile apic_register_t *)map_region(apic_addr, APIC_REGS_SIZE, false);
        if (apic == NULL) {
            num_available_cpus = 1;
            return false;
        }
    }
#endif
    return true;
}

#if defined(__i386__) || defined(__x86_64__)
// The BSP is not always the first CPU listed in the MADT. Make sure it is in
// slot 0, otherwise smp_start() would send INIT-SIPI to the BSP itself,
// hanging the system, and the AP listed in slot 0 would never be started.
static void verify_bsp_is_cpu0(void)
{
    cpu_apic_id_t bsp_apic_id = (cpu_apic_id_t)my_apic_id();

    if (cpu_num_to_apic_id[0] == bsp_apic_id) {
        return;
    }
    for (int i = 1; i < num_available_cpus; i++) {
        if (cpu_num_to_apic_id[i] == bsp_apic_id) {
            cpu_num_to_apic_id[i] = cpu_num_to_apic_id[0];
            break;
        }
    }
    // If the BSP wasn't listed at all, this drops the CPU that was in slot 0.
    cpu_num_to_apic_id[0] = bsp_apic_id;
}
#endif

// Parses the SRAT and populates the proximity-domain arrays.
// Returns 1 on success, 0 if the SRAT is absent or invalid, and -1 if the
// topology declares more than MAX_PROXIMITY_DOMAINS distinct domains. The
// caller must treat any non-1 result as "NUMA disabled" and must not keep
// partially accumulated topology state.
#if VMEM_MAX_CONTEXTS > 1
static int find_numa_nodes_in_srat(void)
{
    uint8_t * tab_entry_ptr;
    // The caller will do fixups.
    if (acpi_config.srat_addr == 0) {
        return 0;
    }

    srat_table_header_t * srat = (srat_table_header_t *)map_region(acpi_config.srat_addr, sizeof(rsdt_header_t), true);
    if (srat == NULL) return 0;

    srat = (srat_table_header_t *)map_region(acpi_config.srat_addr, srat->h.length, true);
    if (srat == NULL) return 0;

    if (acpi_checksum(srat, srat->h.length) != 0) {
        return 0;
    }
    // A table which contains fewer bytes than header + 1 processor affinity
    // entry + 1 memory affinity entry would be very weird.
    if (srat->h.length < sizeof(*srat) + sizeof(srat_memory_affinity_entry_t)) {
        return 0;
    }

    tab_entry_ptr = (uint8_t *)srat + sizeof(*srat);
    uint8_t * srat_table_end = (uint8_t *)srat + srat->h.length;
    // Pass 1: parse memory affinity entries and allocate proximity domains for each of them, while validating input a little bit.
    while (tab_entry_ptr < srat_table_end) {
        srat_entry_header_t *entry_header = (srat_entry_header_t *)tab_entry_ptr;
        if (tab_entry_ptr + sizeof(srat_entry_header_t) > srat_table_end
         || entry_header->length < sizeof(srat_entry_header_t)
         || tab_entry_ptr + entry_header->length > srat_table_end) {
            // A truncated trailing entry must not be parsed: its length byte
            // could match an accepted size and push the read past the end of
            // the table.
            return 0;
        }
#if defined(__aarch64__)
        if (entry_header->type == SRAT_PROCESSOR_GICC_AFFINITY) {
            if (entry_header->length != sizeof(srat_processor_gicc_affinity_entry_t)) {
                return 0;
            }
        }
#else
        if (entry_header->type == SRAT_PROCESSOR_APIC_AFFINITY) {
            if (entry_header->length != sizeof(srat_processor_lapic_affinity_entry_t)) {
                return 0;
            }
        }
        else if (entry_header->type == SRAT_PROCESSOR_X2APIC_AFFINITY) {
            if (entry_header->length != sizeof(srat_processor_lx2apic_affinity_entry_t)) {
                return 0;
            }
        }
#endif
        else if (entry_header->type == SRAT_MEMORY_AFFINITY) {
            if (entry_header->length != sizeof(srat_memory_affinity_entry_t)) {
                return 0;
            }
            srat_memory_affinity_entry_t *entry = (srat_memory_affinity_entry_t *)tab_entry_ptr;
            if (entry->flags & SRAT_MAF_ENABLED) {
                uint32_t proximity_domain = entry->proximity_domain;
                uint64_t start = entry->base_address;
                uint64_t end = entry->base_address + entry->address_length;
                int found = -1;

                if (start > end) {
                    // We've found a wraparound, that's not good.
                    return 0;
                }
                if (start == end) {
                    // A zero-length enabled entry claims no memory; discard
                    // it rather than letting it count as domain ownership.
                    tab_entry_ptr += entry_header->length;
                    continue;
                }
#if defined(__loongarch_lp64)
                // The NUMA address transform relocates the range start's
                // four-bit node field; a range spanning two encoded node
                // values cannot be represented and would alias after the
                // transform. Check the original encoding before any
                // transform has run.
                if (end > 0 && (start >> 44) != ((end - 1) >> 44)) {
                    return 0;
                }
#endif

                // Allocate entry in proximity_domains, if necessary. Linear search for now.
                for (int i = 0; i < num_proximity_domains; i++) {
                    if (proximity_domains[i] == proximity_domain) {
                        found = i;
                        break;
                    }
                }
                if (found == -1) {
                    // Not found, allocate entry.
                    if (num_proximity_domains < (int)(ARRAY_SIZE(proximity_domains))) {
                        proximity_domains[num_proximity_domains] = proximity_domain;
                        found = num_proximity_domains;
                        num_proximity_domains++;
                    } else {
                        // Topology too large: do not truncate the domain list
                        // and continue with inconsistent CPU/memory ownership.
                        return -1;
                    }
                }

                // Now that we have the index of the entry in proximity_domains in found, use it.
                if (num_memory_affinity_ranges < (int)(ARRAY_SIZE(memory_affinity_ranges))) {
                    memory_affinity_ranges[num_memory_affinity_ranges].proximity_domain_idx = (uint32_t)found;
                    memory_affinity_ranges[num_memory_affinity_ranges].start = start;
                    memory_affinity_ranges[num_memory_affinity_ranges].end = end;
                    num_memory_affinity_ranges++;
                } else {
                    // Too many affinity ranges: same clean-fallback policy.
                    return -1;
                }
            }
        }
        else if (entry_header->type == SRAT_PROCESSOR_X2APIC_AFFINITY) {
            if (entry_header->length != sizeof(srat_processor_lx2apic_affinity_entry_t)) {
                return 0;
            }
        } else {
            return 0;
        }
        tab_entry_ptr += entry_header->length;
    }

    tab_entry_ptr = (uint8_t *)srat + sizeof(*srat);
    // Pass 2: parse processor affinity entries and map them to internal CPU
    // ordinals. On x86/LoongArch the CPU is identified by its APIC ID, on
    // AArch64 by its ACPI Processor UID (SRAT type 3).
    while (tab_entry_ptr < srat_table_end) {
        srat_entry_header_t *entry_header = (srat_entry_header_t *)tab_entry_ptr;
        if (tab_entry_ptr + sizeof(srat_entry_header_t) > srat_table_end
         || entry_header->length < sizeof(srat_entry_header_t)
         || tab_entry_ptr + entry_header->length > srat_table_end) {
            // Defensive: pass 1 already validated the table, but never walk
            // a truncated trailing entry.
            return 0;
        }
        uint32_t proximity_domain;
        uint32_t apic_id;
#if defined(__aarch64__)
        if (entry_header->type == SRAT_PROCESSOR_GICC_AFFINITY) {
            srat_processor_gicc_affinity_entry_t *entry = (srat_processor_gicc_affinity_entry_t *)tab_entry_ptr;
            if (entry->flags & SRAT_GICC_AAF_ENABLED) {
                int found1;
                proximity_domain = entry->proximity_domain;
                apic_id = entry->acpi_processor_uid;

                found1 = -1;
                // Find entry in proximity_domains, if necessary. Linear search for now.
                for (int i = 0; i < num_proximity_domains; i++) {
                    if (proximity_domains[i] == proximity_domain) {
                        found1 = i;
                        break;
                    }
                }
                if (found1 == -1) {
                    // A CPU-only domain (no memory entry): represent it so
                    // its CPUs are parked instead of invalidating the whole
                    // topology.
                    if (num_proximity_domains < (int)(ARRAY_SIZE(proximity_domains))) {
                        proximity_domains[num_proximity_domains] = proximity_domain;
                        found1 = num_proximity_domains;
                        num_proximity_domains++;
                    } else {
                        // Topology too large: do not truncate the domain list
                        // and continue with inconsistent CPU/memory ownership.
                        return -1;
                    }
                }

                // Do we know about that ACPI Processor UID ? Only populated
                // slots count; an unassigned slot still carries UID 0.
                int found2 = -1;
                for (int i = 0; i < num_available_cpus; i++) {
                    if (cpu_num_to_uid_assigned[i] && cpu_num_to_acpi_uid[i] == apic_id) {
                        found2 = i;
                        break;
                    }
                }

                if (found2 == -1) {
                    // We've found an affinity entry whose CPU we don't know about.
                    return 0;
                }

                // Reject a second affinity entry for the same CPU: duplicate or
                // conflicting affinities both make the mapping ambiguous.
                if (cpu_num_to_domain_assigned[found2]) {
                    return 0;
                }
                cpu_num_to_domain_assigned[found2] = true;
                cpu_num_to_proximity_domain_idx[found2] = (uint32_t)found1;
            }
        }
#else
        if (entry_header->type == SRAT_PROCESSOR_APIC_AFFINITY) {
            srat_processor_lapic_affinity_entry_t *entry = (srat_processor_lapic_affinity_entry_t *)tab_entry_ptr;
            if (entry->flags & SRAT_PAAF_ENABLED) {
                int found1;
                proximity_domain = ((uint32_t)entry->proximity_domain_high) << 8 | entry->proximity_domain_low;
                apic_id = (uint32_t)entry->apic_id;

find_proximity_domain:
                found1 = -1;
                // Find entry in proximity_domains, if necessary. Linear search for now.
                for (int i = 0; i < num_proximity_domains; i++) {
                    if (proximity_domains[i] == proximity_domain) {
                        found1 = i;
                        break;
                    }
                }
                if (found1 == -1) {
                    // A CPU-only domain (no memory entry): represent it so
                    // its CPUs are parked instead of invalidating the whole
                    // topology.
                    if (num_proximity_domains < (int)(ARRAY_SIZE(proximity_domains))) {
                        proximity_domains[num_proximity_domains] = proximity_domain;
                        found1 = num_proximity_domains;
                        num_proximity_domains++;
                    } else {
                        // Topology too large: do not truncate the domain list
                        // and continue with inconsistent CPU/memory ownership.
                        return -1;
                    }
                }

                // Do we know about that APIC ID ?
                int found2 = -1;
                for (int i = 0; i < num_available_cpus; i++) {
                    if (cpu_num_to_apic_id[i] == apic_id) {
                        found2 = i;
                        break;
                    }
                }

                if (found2 == -1) {
                    // We've found an affinity entry whose APIC ID we don't know about.
                    return 0;
                }

                // Reject a second affinity entry for the same CPU: duplicate or
                // conflicting affinities both make the mapping ambiguous.
                if (cpu_num_to_domain_assigned[found2]) {
                    return 0;
                }
                cpu_num_to_domain_assigned[found2] = true;
                cpu_num_to_proximity_domain_idx[found2] = (uint32_t)found1;
            }
        }
        else if (entry_header->type == SRAT_PROCESSOR_X2APIC_AFFINITY) {
            srat_processor_lx2apic_affinity_entry_t *entry = (srat_processor_lx2apic_affinity_entry_t *)tab_entry_ptr;
            if (entry->flags & SRAT_PXAAF_ENABLED) {
                proximity_domain = entry->proximity_domain;
                apic_id = entry->apic_id;
                goto find_proximity_domain;
            }
        }
#endif
        tab_entry_ptr += entry_header->length;
    }

    return 1;
}
#endif

// Sorts the accepted memory-affinity ranges by (start, end) and validates
// that ranges of different proximity domains never overlap. Returns false
// (and leaves the caller to disable NUMA) when ownership is ambiguous.
// Called after any architecture-specific range transform.
#if VMEM_MAX_CONTEXTS > 1
static bool sort_and_validate_memory_affinity_ranges(void)
{
    // Insertion sort by (start, end).
    for (int i = 1; i < num_memory_affinity_ranges; i++) {
        memory_affinity_t key = memory_affinity_ranges[i];
        int j = i - 1;
        while (j >= 0 && (   memory_affinity_ranges[j].start > key.start
                          || (memory_affinity_ranges[j].start == key.start && memory_affinity_ranges[j].end > key.end))) {
            memory_affinity_ranges[j + 1] = memory_affinity_ranges[j];
            j--;
        }
        memory_affinity_ranges[j + 1] = key;
    }

    // An empty enabled set (after discarding zero-length entries) must not
    // manufacture a phantom range: the merge below would promote the
    // initialized [0,0) slot to a real entry. Disable NUMA instead.
    if (num_memory_affinity_ranges == 0) {
        return false;
    }

    // Merge overlapping or adjacent ranges of the same domain: the mapping
    // consumes the union of a domain's spans, so the progress accounting
    // must operate on the same interval set. The ranges are sorted by
    // (start, end), so a single pass suffices.
    int out = 0;
    for (int i = 1; i < num_memory_affinity_ranges; i++) {
        memory_affinity_t *prev = &memory_affinity_ranges[out];
        memory_affinity_t *cur  = &memory_affinity_ranges[i];
        if (cur->proximity_domain_idx == prev->proximity_domain_idx && cur->start <= prev->end) {
            if (cur->end > prev->end) {
                prev->end = cur->end;
            }
        } else {
            memory_affinity_ranges[++out] = *cur;
        }
    }
    num_memory_affinity_ranges = out + 1;

    // Overlapping ranges of different domains make physical ownership
    // ambiguous; two contexts must never test aliases of the same span.
    // With sorted starts, range i overlaps any previous range j with
    // start_i < end_j, not only the immediately preceding one: nested or
    // chained same-domain ranges must not hide a cross-domain overlap.
    for (int i = 1; i < num_memory_affinity_ranges; i++) {
        for (int j = 0; j < i; j++) {
            if (   memory_affinity_ranges[i].start < memory_affinity_ranges[j].end
                && memory_affinity_ranges[i].proximity_domain_idx != memory_affinity_ranges[j].proximity_domain_idx) {
                return false;
            }
        }
    }
    return true;
}
#endif

#if 0
static bool parse_slit(uintptr_t slit_addr)
{
    // SLIT is a simple table.

    // SLIT Header is identical to RSDP Header
    rsdt_header_t *slit = (rsdt_header_t *)slit_addr;

    // Validate SLIT
    if (slit == NULL || acpi_checksum(slit, slit->length) != 0) {
        return false;
    }
    // A SLIT shall always contain at least one byte beyond the header and the number of localities.
    if (slit->length <= sizeof(*slit) + sizeof(uint64_t)) {
        return false;
    }
    // 8 bytes for the number of localities, followed by (number of localities) ^ 2 bytes.
    uint64_t localities = *(uint64_t *)((uint8_t *)slit + sizeof(*slit));
    if (localities > MAX_APIC_IDS) {
        return false;
    }
    if (slit->length != sizeof(*slit) + sizeof(uint64_t) + (localities * localities)) {
        return false;
    }

    return true;
}
#endif

#if !defined(__aarch64__)
static inline void send_ipi(int apic_id, int trigger __attribute__((unused)), int level __attribute__((unused)), int mode, uint8_t vector)
{
#if defined(__i386__) || defined(__x86_64__)
    if (apic_x2apic) {
        uint64_t icr = ((uint64_t)apic_id << 32) | (uint32_t)(trigger << 15 | level << 14 | mode << 8 | vector);
        // The x2APIC ICR WRMSR is not serializing; fence so older stores are visible first (SDM vol 3A 11.12.3).
        __asm__ __volatile__ ("mfence; lfence" : : : "memory");
        wrmsr(MSR_IA32_X2APIC_BASE + APIC_REG_ICRLO, (uint32_t)icr, (uint32_t)(icr >> 32));
        return;
    }

    apic_write(APIC_REG_ICRHI, apic_id << 24);
    apic_write(APIC_REG_ICRLO, trigger << 15 | level << 14 | mode << 8 | vector);
#elif defined(__loongarch_lp64)
    if (mode == APIC_DELMODE_STARTUP) {
        //
        // Set the AP mailbox0
        //
        __iocsrwr_d((1ULL << 31 | ((0x0 << 1) + 1) << 2 | apic_id << 16 | (((uintptr_t)ap_startup_addr) & 0xFFFFFFFF00000000ULL)), 0x1048);
        __iocsrwr_d((1ULL << 31 | (0x0 << 1) << 2 | apic_id << 16 | (((uintptr_t)ap_startup_addr << 32))), 0x1048);
    }

    //
    // Trigger IPI
    //
    __iocsrwr_d((1<<31 | apic_id << 16 | vector), 0x1040);
#endif
}

static bool send_ipi_and_wait(int apic_id, int trigger, int level, int mode, uint8_t vector, int delay_before_poll)
{
    send_ipi(apic_id, trigger, level, mode, vector);

    usleep(delay_before_poll);

    // Wait for send complete or timeout after 100ms.
    int timeout = 1000;
#if defined(__i386__) || defined(__x86_64__)
    while (timeout > 0) {
        bool send_pending = (apic_read(APIC_REG_ICRLO) & APIC_ICR_BUSY);
        if (!send_pending) {
            return true;
        }
        usleep(100);
        timeout--;
    }
    return false;
#elif defined(__loongarch_lp64)
    while (timeout > 0) {
        usleep(100);
        timeout--;
    }
    return true;
#endif
}
#endif // !defined(__aarch64__)

#if defined(__i386__) || defined(__x86_64__)
static uint32_t read_apic_esr(bool is_p5)
{
    if (!is_p5) {
        apic_write(APIC_REG_ESR, 0);
    }
    return apic_read(APIC_REG_ESR);
}

static bool start_cpu(int cpu_num)
{
    // This is based on the method used in Linux 5.14.
    // We don't support non-integrated APICs, so can simplify it a bit.

    int apic_id = cpu_num_to_apic_id[cpu_num];

    uint32_t apic_ver = apic_read(APIC_REG_VER);
    uint32_t max_lvt = (apic_ver >> 16) & 0x7f;
    bool is_p5 = (max_lvt == 3);

    bool use_long_delays = true;
    if ((cpuid_info.vendor_id.str[0] == 'G' && cpuid_info.version.family == 6)      // Intel P6 or later
    ||  (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.family >= 15)) {  // AMD Hammer or later
        use_long_delays = false;
    }

    // Clear APIC errors.
    (void)read_apic_esr(is_p5);

    // Pulse the INIT IPI.
    if (!send_ipi_and_wait(apic_id, APIC_TRIGGER_LEVEL, 1, APIC_DELMODE_INIT, 0, 0)) {
        return false;
    }
    if (use_long_delays) {
        usleep(10*1000);  // 10ms
    }
    if (!send_ipi_and_wait(apic_id, APIC_TRIGGER_LEVEL, 0, APIC_DELMODE_INIT, 0, 0)) {
        return false;
    }

    // Send two STARTUP_IPIs.
    for (int num_sipi = 0; num_sipi < 2; num_sipi++) {
        // Clear APIC errors.
        (void)read_apic_esr(is_p5);

        // Send the STARTUP IPI.
        if (!send_ipi_and_wait(apic_id, 0, 0, APIC_DELMODE_STARTUP, AP_TRAMPOLINE_PAGE, use_long_delays ? 300 : 10)) {
            return false;
        }

        // Give the other CPU some time to accept the IPI.
        usleep(use_long_delays ? 200 : 10);

        // Check the IPI was accepted.
        uint32_t status = read_apic_esr(is_p5) & 0xef;
        if (status != 0) {
            return false;
        }
    }

    return true;
}
#elif defined(__loongarch_lp64)
static bool start_cpu(int cpu_num)
{
    int apic_id = cpu_num_to_apic_id[cpu_num];
    bool use_long_delays = false;

    // Send the STARTUP IPI.
    if (!send_ipi_and_wait(apic_id, 0, 0, APIC_DELMODE_STARTUP, 0, use_long_delays ? 300 : 10)) {
        return false;
    }
    // Give the other CPU some time to accept the IPI.
    usleep(use_long_delays ? 200 : 10);

    return true;
}
#elif defined(__aarch64__)
static bool start_cpu(int cpu_num)
{
    // The AP enters startup64 with the MMU off and its CPU number in x0.
    int64_t status = psci_cpu_on(cpu_num_to_apic_id[cpu_num], (uintptr_t)ap_startup_addr, cpu_num);

    return (status == PSCI_RET_SUCCESS);
}
#endif

#if defined(__loongarch_lp64)
uint8_t checkout_max_memory_bits_of_this_numa_node(unsigned int range)
{
    uint64_t max_memory_range = memory_affinity_ranges[range].end & (~(0xFULL << 44));
    uint8_t  bits = 0;

    if (max_memory_range > 0x0) {
        do {
            bits++;
            max_memory_range = max_memory_range >> 1;
        } while (max_memory_range > 0x1);
    } else {
        return 0;
    }

    return bits;
}

#if VMEM_MAX_CONTEXTS > 1
// The node bits of the LoongArch64 node-in-address encoding.
#define NUMA_NODE_OFFSET 44

// Transforms the node-in-address encoding into a contiguous virtual layout
// by moving the 4-bit node to just above the memory bits. Returns false
// without mutating anything when a span crosses an encoded-node boundary:
// attributing it to the start's node would push the later node's memory
// beyond the pm_map enumeration, silently omitting it. The caller keeps the
// identity mapping in that case, which still tests every range.
bool map_the_numa_memory_range(unsigned int highest_bit)
{
    unsigned int i;

    // Validate every span before mutating anything: the transform must be
    // atomic, or a failure in a later span would leave the earlier entries
    // canonicalized while the identity mapping is selected.
    for (i = 0; i < pm_map_size; i++) {
        uint8_t node_s = (pm_map[i].start >> (NUMA_NODE_OFFSET - PAGE_SHIFT)) & 0xF;
        uint8_t node_e = (pm_map[i].end >> (NUMA_NODE_OFFSET - PAGE_SHIFT)) & 0xF;
        if (node_s != node_e) {
            return false;
        }
    }
    for (i = 0; i < num_memory_affinity_ranges; i++) {
        if (memory_affinity_ranges[i].proximity_domain_idx != 0) {
            uint8_t node_s = (memory_affinity_ranges[i].start >> NUMA_NODE_OFFSET) & 0xF;
            uint8_t node_e = (memory_affinity_ranges[i].end >> NUMA_NODE_OFFSET) & 0xF;
            if (node_s != node_e) {
                return false;
            }
        }
    }

    // All spans validated: now mutate.
    for (i = 0; i < pm_map_size; i++) {
        uint8_t node_s = (pm_map[i].start >> (NUMA_NODE_OFFSET - PAGE_SHIFT)) & 0xF;
        if (node_s != 0) {
            pm_map[i].start &= ~((uint64_t)0xF << (NUMA_NODE_OFFSET - PAGE_SHIFT));
            pm_map[i].start |= (uint64_t)node_s << (highest_bit - PAGE_SHIFT);

            pm_map[i].end &= ~((uint64_t)0xF << (NUMA_NODE_OFFSET - PAGE_SHIFT));
            pm_map[i].end |= (uint64_t)node_s << (highest_bit - PAGE_SHIFT);
        }
    }
    for (i = 0; i < num_memory_affinity_ranges; i++) {
        if (memory_affinity_ranges[i].proximity_domain_idx != 0) {
            uint8_t node_s = (memory_affinity_ranges[i].start >> NUMA_NODE_OFFSET) & 0xF;
            if (node_s != 0) {
                memory_affinity_ranges[i].start &= ~((uint64_t)0xF << NUMA_NODE_OFFSET);
                memory_affinity_ranges[i].start |= (uint64_t)node_s << highest_bit;
                memory_affinity_ranges[i].end   &= ~((uint64_t)0xF << NUMA_NODE_OFFSET);
                memory_affinity_ranges[i].end   |= (uint64_t)node_s << highest_bit;
            }
        }
    }
    return true;
}
#endif

#if VMEM_MAX_CONTEXTS > 1
void check_if_needs_to_map(void)
{
    unsigned int i;
    uint8_t  local_memory_area_bits;
    uint8_t  max_memory_bits = 0x0;

    if (num_proximity_domains == 0x0) {
        return;
    } else {
        for (i = 0; i < num_memory_affinity_ranges; i++) {
            if (memory_affinity_ranges[i].proximity_domain_idx != 0) {
                local_memory_area_bits = checkout_max_memory_bits_of_this_numa_node(i);
                if (max_memory_bits < local_memory_area_bits) {
                    max_memory_bits = local_memory_area_bits;
                }
            }
        }
        // The transform places the 4-bit node at highest_map_bit; it must
        // stay within the address width or the shifts below would be
        // undefined. max_memory_bits <= 59 keeps highest_map_bit <= 60.
        if (max_memory_bits > 0 && max_memory_bits <= 59) {
            highest_map_bit = max_memory_bits + 1;
            // Validate before mutating: the transform must round-trip (no
            // pm_map span may cross an encoded-node boundary) and the
            // transformed spans must stay disjoint, or two teams would
            // alias the same test virtual addresses and later nodes could
            // fall beyond the pm_map enumeration. On failure the identity
            // mapping remains in force and every range is still tested.
            map_numa_memory_range = smp_numa_transform_valid()
                                 && map_the_numa_memory_range(highest_map_bit);
        }
    }
}
#endif

// Validates the NUMA address transform for parallel teams. The original
// "range stays within one encoded node value" check ran in
// find_numa_nodes_in_srat() before any transform; here the transformed
// spans of different ranges must not overlap, or two teams would alias the
// same test virtual addresses.
bool smp_numa_transform_valid(void)
{
    // Runs before the transform mutates the ranges: the node bits are read
    // from their original position and the transformed spans are computed
    // on the fly, exactly what the mutation produces afterwards.
    for (int i = 0; i < num_memory_affinity_ranges; i++) {
        uint64_t node_nu = (memory_affinity_ranges[i].start >> NUMA_NODE_OFFSET) & 0xF;
        uint64_t start = memory_affinity_ranges[i].start & ~(0xFULL << NUMA_NODE_OFFSET);
        uint64_t end   = memory_affinity_ranges[i].end   & ~(0xFULL << NUMA_NODE_OFFSET);
        start |= node_nu << highest_map_bit;
        end   |= node_nu << highest_map_bit;
        for (int j = 0; j < i; j++) {
            uint64_t node_nu_j = (memory_affinity_ranges[j].start >> NUMA_NODE_OFFSET) & 0xF;
            uint64_t j_start = memory_affinity_ranges[j].start & ~(0xFULL << NUMA_NODE_OFFSET);
            uint64_t j_end   = memory_affinity_ranges[j].end   & ~(0xFULL << NUMA_NODE_OFFSET);
            j_start |= node_nu_j << highest_map_bit;
            j_end   |= node_nu_j << highest_map_bit;
            if (j_start < end && j_end > start) {
                // Transformed spans overlap: two teams would alias the same
                // test virtual addresses.
                return false;
            }
        }
    }
    return true;
}
#else
void check_if_needs_to_map(void)
{
    //
    // It is an empty function if not LoongArch64.
    //
    return;
}

bool smp_numa_transform_valid(void)
{
    return true;
}
#endif

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void smp_init(bool smp_enable)
{
#if VMEM_MAX_CONTEXTS > 1
    for (int i = 0; i < (int)(ARRAY_SIZE(cpu_num_to_proximity_domain_idx)); i++) {
        cpu_num_to_proximity_domain_idx[i] = 0;
    }
#endif
    for (int i = 0; i < (int)(ARRAY_SIZE(cpu_num_to_apic_id)); i++) {
        cpu_num_to_apic_id[i] = 0;
    }
#if defined(__aarch64__) && VMEM_MAX_CONTEXTS > 1
    for (int i = 0; i < (int)(ARRAY_SIZE(cpu_num_to_acpi_uid)); i++) {
        cpu_num_to_acpi_uid[i] = 0;
    }
    for (int i = 0; i < (int)(ARRAY_SIZE(cpu_num_to_uid_assigned)); i++) {
        cpu_num_to_uid_assigned[i] = false;
    }
#endif
#if VMEM_MAX_CONTEXTS > 1
    for (int i = 0; i < (int)(ARRAY_SIZE(cpu_num_to_domain_assigned)); i++) {
        cpu_num_to_domain_assigned[i] = false;
    }

    for (int i = 0; i < (int)(ARRAY_SIZE(memory_affinity_ranges)); i++) {
        memory_affinity_ranges[i].proximity_domain_idx = UINT32_C(0xFFFFFFFF);
        memory_affinity_ranges[i].start = 0;
        memory_affinity_ranges[i].end = 0;
    }
#endif

    for (int i = 0; i < (int)(ARRAY_SIZE(used_cpus_in_proximity_domain)); i++) {
        used_cpus_in_proximity_domain[i] = 0;
    }

    num_available_cpus = 1;
#if VMEM_MAX_CONTEXTS > 1
    num_memory_affinity_ranges = 0;
    num_proximity_domains = 0;
#endif

#if defined(__i386__) || defined(__x86_64__)
    apic_x2apic = false;
    if (cpuid_info.flags.x2apic) {
        uint32_t msrl, msrh;
        rdmsr(MSR_IA32_APIC_BASE, msrl, msrh);
        if ((msrl & IA32_APIC_ENABLED) && (msrl & IA32_APIC_EXTENDED)) {
            apic_x2apic = true;
        }
    }
#endif

    // Process SMP Quirks
    if (quirk.type & QUIRK_TYPE_SMP) {
        // quirk.process();
        smp_enable = false;
    }

    if (smp_enable) {
#if defined(__i386__) || defined(__x86_64__)
        (void)(find_cpus_in_madt() || find_cpus_in_floating_mp_struct());
        if (apic != NULL || apic_x2apic) {
            verify_bsp_is_cpu0();
        }
#else
        find_cpus_in_madt();
#endif
    }

#if VMEM_MAX_CONTEXTS > 1
    if (smp_enable) {
        int srat_status = find_numa_nodes_in_srat();
        if (srat_status > 0) {
            check_if_needs_to_map();
            // Sort and validate after any architecture-specific range
            // transform; ambiguous ownership disables NUMA entirely.
            if (!sort_and_validate_memory_affinity_ranges()) {
                srat_status = 0;
            }
        }
        if (srat_status <= 0) {
            // Do not keep partially accumulated topology state: NUMA must
            // never run with inconsistent CPU/memory ownership.
            num_proximity_domains = 0;
            num_memory_affinity_ranges = 0;
            smp_topology_too_large = (srat_status < 0);
        }
    }
#endif


    // Allocate two pages of low memory for the AP trampoline and sync
    // objects. These need to remain pinned in place during relocation.
    smp_heap_page = heap_alloc(HEAP_TYPE_LM_1, SYNC_ARENA_PAGES * PAGE_SIZE, PAGE_SIZE) >> PAGE_SHIFT;

#if defined(__i386__) || defined(__x86_64__)
    alloc_addr = HEAP_BASE_ADDR + (ap_trampoline_end - ap_trampoline);
#elif defined(__loongarch_lp64) || defined(__aarch64__)
    alloc_addr = HEAP_BASE_ADDR;
#endif
}

int smp_start(cpu_state_t cpu_state[MAX_CPUS])
{
    int cpu_num;

    // Set up the AP startup vector here rather than in smp_init(): the program
    // may have been relocated in between, and the APs must enter the running copy.
#if defined(__i386__) || defined(__x86_64__)
    ap_startup_addr = (uintptr_t)startup;

    memcpy((uint8_t *)HEAP_BASE_ADDR, ap_trampoline, ap_trampoline_end - ap_trampoline);
#elif defined(__loongarch_lp64)
    ap_startup_addr = (uintptr_t)startup64;
#elif defined(__aarch64__)
    ap_startup_addr = (uintptr_t)startup64;

    // The APs boot with MMU and caches off, so make the (possibly relocated)
    // program image visible at the point of coherency before waking them.
    cache_clean_range(_start, _end);
#endif

    cpu_state[0] = CPU_STATE_RUNNING;  // we don't support disabling the boot CPU

    for (cpu_num = 1; cpu_num < num_available_cpus; cpu_num++) {
        if (cpu_state[cpu_num] == CPU_STATE_ENABLED) {
            if (!start_cpu(cpu_num)) {
                return cpu_num;
            }
        }
#if SEQUENTIAL_AP_START
        int timeout = 10*1000*10;
        while (timeout > 0) {
            if (cpu_state[cpu_num] == CPU_STATE_RUNNING) break;
            usleep(100);
            timeout--;
        }
        if (cpu_state[cpu_num] != CPU_STATE_RUNNING) {
            return cpu_num;
        }
#endif
    }

#if SEQUENTIAL_AP_START
    return 0;
#else
    int timeout = 10*1000*10;
    while (timeout > 0) {
        for (cpu_num = 1; cpu_num < num_available_cpus; cpu_num++) {
            if (cpu_state[cpu_num] == CPU_STATE_ENABLED) break;
        }
        if (cpu_num == num_available_cpus) {
            return 0;
        }
        usleep(100);
        timeout--;
    }
    return cpu_num;
#endif
}

void smp_send_nmi(int cpu_num __attribute__((unused)))
{
#if defined(__aarch64__)
    // Wake up all CPUs waiting in WFE. The waiters recheck their wakeup flag,
    // so waking more CPUs than necessary is harmless. The DSB ensures the flag
    // update is visible before the event, or the wakeup could be missed.
    __asm__ __volatile__ ("dsb ish; sev" ::: "memory");
#else
#if defined(__i386__) || defined(__x86_64__)
    while (apic_read(APIC_REG_ICRLO) & APIC_ICR_BUSY) {
        __builtin_ia32_pause();
    }
#endif
    send_ipi(cpu_num_to_apic_id[cpu_num], 0, 0, APIC_DELMODE_NMI, 0);
#endif
}

int smp_my_cpu_num(void)
{
#if defined(__aarch64__)
    // Our CPU number was stored in TPIDR_EL1 by the startup code.
    return (int)read_sysreg(tpidr_el1);
#else
    if (num_available_cpus <= 1) return 0;

    int apic_id = my_apic_id();
    for (int i = 0; i < num_available_cpus; i++) {
        if ((int)cpu_num_to_apic_id[i] == apic_id) {
            return i;
        }
    }
    return 0;
#endif
}

#if VMEM_MAX_CONTEXTS > 1
uint32_t smp_get_proximity_domain_idx(int cpu_num)
{
    return num_available_cpus > 1 ? cpu_num_to_proximity_domain_idx[cpu_num] : 0;
}
#endif

// Computes the first span, limited to a single proximity domain, of the given
// memory range. The memory-affinity ranges are sorted by (start, end): a
// range that starts after the query is proof of an uncovered gap, so a gap
// never falls through to be misattributed to a later range.
#if VMEM_MAX_CONTEXTS > 1
int smp_narrow_to_proximity_domain(uint64_t start, uint64_t end, uint32_t * proximity_domain_idx, uint64_t * new_start, uint64_t * new_end)
{
    for (int i = 0; i < num_memory_affinity_ranges; i++) {
        uint64_t range_start = memory_affinity_ranges[i].start;
        uint64_t range_end = memory_affinity_ranges[i].end;

        if (range_end <= start) {
            // The range lies entirely before the query.
            continue;
        }
        if (range_start >= end) {
            // Sorted ranges: no further range can intersect the query.
            break;
        }
        if (start < range_start) {
            // [start, range_start) is an uncovered gap, not memory of this
            // range. The caller falls back to legacy topology-agnostic
            // handling for the remaining span.
            return 0;
        }
        // range_start <= start < range_end: the query overlaps this range.
        *proximity_domain_idx = memory_affinity_ranges[i].proximity_domain_idx;
        *new_start = start;
        *new_end = end <= range_end ? end : range_end;
        return 1;
    }
    // No range intersects the query.
    return 0;
}
#endif

// Returns true if the given proximity-domain index owns at least one
// accepted SRAT memory range.
#if VMEM_MAX_CONTEXTS > 1
bool smp_domain_has_memory(uint32_t domain_idx)
{
    for (int i = 0; i < num_memory_affinity_ranges; i++) {
        if (memory_affinity_ranges[i].proximity_domain_idx == domain_idx) {
            return true;
        }
    }
    return false;
}
#endif

// Returns the number of the given domain's SRAT range pieces intersecting
// the given page range; each intersection is one vm_map segment.
#if VMEM_MAX_CONTEXTS > 1
uint32_t smp_domain_memory_pieces_in_range(uint32_t domain_idx, uintptr_t start_page, uintptr_t end_page)
{
    uint32_t pieces = 0;
    for (int i = 0; i < num_memory_affinity_ranges; i++) {
        if (memory_affinity_ranges[i].proximity_domain_idx != domain_idx) {
            continue;
        }
        uint64_t s = memory_affinity_ranges[i].start >> PAGE_SHIFT;
        uint64_t e = memory_affinity_ranges[i].end >> PAGE_SHIFT;
        if (s < end_page && e > start_page) {
            pieces++;
        }
    }
    return pieces;
}
#endif

#if 0
void get_memory_affinity_entry(int idx, uint32_t * proximity_domain_idx, uint64_t * start, uint64_t * end)
{
    *proximity_domain_idx = memory_affinity_ranges[idx].proximity_domain_idx;
    *start = memory_affinity_ranges[idx].start;
    *end = memory_affinity_ranges[idx].end;
}
#endif

barrier_t *smp_alloc_barriers_(unsigned int num_barriers, unsigned int num_threads)
{
    size_t size = (size_t)num_barriers * sizeof(barrier_t);
    if (alloc_addr + size > SYNC_ARENA_LIMIT) {
        // Do not overwrite adjacent low memory; report exhaustion so the
        // caller can disable SMP/NUMA_PAR cleanly.
        alloc_addr = SYNC_ARENA_LIMIT;
        return NULL;
    }
    barrier_t *barriers = (barrier_t *)(alloc_addr);
    alloc_addr += size;
    for (unsigned int i = 0; i < num_barriers; i++) {
        barrier_init(&barriers[i], num_threads);
    }
    return barriers;
}

spinlock_t *smp_alloc_mutex()
{
    if (alloc_addr + sizeof(spinlock_t) > SYNC_ARENA_LIMIT) {
        // A shared global lock remains correct if the arena is exhausted.
        return &fallback_mutex;
    }
    spinlock_t *mutex = (spinlock_t *)(alloc_addr);
    alloc_addr += sizeof(spinlock_t);
    spin_unlock(mutex);
    return mutex;
}
