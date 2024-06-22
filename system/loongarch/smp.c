// SPDX-License-Identifier: GPL-2.0

#include <stdbool.h>
#include <stdint.h>

#include <larchintrin.h>

#include "acpi.h"
#include "boot.h"
#include "macros.h"
#include "bootparams.h"
#include "efi.h"

#include "cpuid.h"
#include "heap.h"
#include "memrw32.h"
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

#define MAX_APIC_IDS                256

#define APIC_REGS_SIZE              SIZE_C(4,KB)

// APIC delivery modes

#define APIC_DELMODE_FIXED          0
#define APIC_DELMODE_LOWEST         1
#define APIC_DELMODE_SMI            2
#define APIC_DELMODE_NMI            4
#define APIC_DELMODE_INIT           5
#define APIC_DELMODE_STARTUP        6
#define APIC_DELMODE_EXTINT         7


// MADT entry types

#define MADT_CORE_PIC               17

// MADT processor flag values

#define MADT_PF_ENABLED             0x1
#define MADT_PF_ONLINE_CAPABLE      0x2

// SRAT entry types

#define SRAT_PROCESSOR_APIC_AFFINITY   0
#define SRAT_MEMORY_AFFINITY           1
#define SRAT_PROCESSOR_X2APIC_AFFINITY 2

// SRAT flag values
#define SRAT_PAAF_ENABLED              1
#define SRAT_MAF_ENABLED               1
#define SRAT_PXAAF_ENABLED             1

// Private memory heap used for AP trampoline and synchronisation objects

#define HEAP_BASE_ADDR              (smp_heap_page << PAGE_SHIFT)

#define AP_TRAMPOLINE_PAGE          (smp_heap_page)

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef volatile uint32_t apic_register_t[4];

typedef struct __attribute__((packed)) {
    uint32_t proximity_domain_idx;
    uint64_t start;
    uint64_t end;
} memory_affinity_t;

typedef struct {
    char        signature[4];   // "APIC"
    uint32_t    length;
    uint8_t     revision;
    uint8_t     checksum;
    char        oem_id[6];
    char        oem_table_id[8];
    char        oem_revision[4];
    char        creator_id[4];
    char        creator_revision[4];
    uint32_t    lapic_addr;
    uint32_t    flags;
} madt_table_header_t;

typedef struct {
    uint8_t     type;
    uint8_t     length;
} madt_entry_header_t;

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

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static apic_register_t  *apic = NULL;

static uint32_t         apic_id_to_cpu_num[MAX_APIC_IDS];

static uint8_t           apic_id_to_proximity_domain_idx[MAX_APIC_IDS];

static uint32_t         cpu_num_to_apic_id[MAX_CPUS];

static memory_affinity_t memory_affinity_ranges[MAX_APIC_IDS];

static uint32_t          proximity_domains[MAX_PROXIMITY_DOMAINS];

static uint8_t           cpus_in_proximity_domain[MAX_PROXIMITY_DOMAINS];
uint8_t                  used_cpus_in_proximity_domain[MAX_PROXIMITY_DOMAINS];

static uintptr_t        smp_heap_page = 0;

static uintptr_t        alloc_addr = 0;

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int num_available_cpus = 1;  // There is always at least one CPU, the BSP
int num_memory_affinity_ranges = 0;
int num_proximity_domains = 0;
bool map_numa_memory_range = false;
uint8_t highest_map_bit = 0;


//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------
//

static inline void send_ipi(int apic_id, int mode, uint8_t vector)
{
    if (mode == APIC_DELMODE_STARTUP) {
        //
        // Set the AP mailbox0
        //
        __iocsrwr_d((1ULL << 31 | ((0x0 << 1) + 1) << 2 | apic_id << 16 | (((uintptr_t)ap_startup_addr) & 0xFFFFFFFF00000000ULL)), 0x1048);
        __iocsrwr_d((1ULL << 31 | (0x0 << 1) << 2 | apic_id << 16 | (((uintptr_t)ap_startup_addr << 32))), 0x1048);
    }

    //
    // Trigger IPI interrupt
    //
    __iocsrwr_d((1<<31 | apic_id << 16 | vector), 0x1040);
}

static bool send_ipi_and_wait(int apic_id, int mode, uint8_t vector, int delay_before_poll)
{
    send_ipi(apic_id, mode, vector);

    usleep(delay_before_poll);

    // Wait for send complete or timeout after 100ms.
    int timeout = 1000;
    while (timeout > 0) {
        usleep(100);
        timeout--;
    }
    return true;
}

static int my_apic_id(void)
{
    return ((int)__csrrd_w(0x20));
}

static bool start_cpu(int cpu_num)
{
    int apic_id = cpu_num_to_apic_id[cpu_num];
    bool use_long_delays = false;

    // Send the STARTUP IPI.
    if (!send_ipi_and_wait(apic_id, APIC_DELMODE_STARTUP, 0, use_long_delays ? 300 : 10)) {
        return false;
    }
    // Give the other CPU some time to accept the IPI.
    usleep(use_long_delays ? 200 : 10);

    return true;
}

static bool find_cpus_in_madt(void)
{
    if (acpi_config.madt_addr == 0) {
        return false;
    }

    madt_table_header_t *mpc = (madt_table_header_t *)map_region(acpi_config.madt_addr, sizeof(madt_table_header_t), true);
    if (mpc == NULL) return false;

    mpc = (madt_table_header_t *)map_region(acpi_config.madt_addr, mpc->length, true);
    if (mpc == NULL) return false;

    if (acpi_checksum(mpc, mpc->length) != 0) {
        return false;
    }

    uintptr_t apic_addr = mpc->lapic_addr;

    int found_cpus = 0;

    uint8_t *tab_entry_ptr = (uint8_t *)mpc + sizeof(madt_table_header_t);
    uint8_t *mpc_table_end = (uint8_t *)mpc + mpc->length;
    while (tab_entry_ptr < mpc_table_end) {
        madt_entry_header_t *entry_header = (madt_entry_header_t *)tab_entry_ptr;
        if (entry_header->type == MADT_CORE_PIC) {
            madt_processor_entry_t *entry = (madt_processor_entry_t *)tab_entry_ptr;
            if (entry->flags & (MADT_PF_ENABLED|MADT_PF_ONLINE_CAPABLE)) {
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
        tab_entry_ptr += entry_header->length;
    }

    apic = (volatile apic_register_t *)map_region(apic_addr, APIC_REGS_SIZE, false);
    if (apic == NULL) {
        num_available_cpus = 1;
        return false;
    }
    return true;
}

static bool find_numa_nodes_in_srat(void)
{
    uint8_t * tab_entry_ptr;
    // The caller will do fixups.
    if (acpi_config.srat_addr == 0) {
        return false;
    }

    srat_table_header_t * srat = (srat_table_header_t *)map_region(acpi_config.srat_addr, sizeof(rsdt_header_t), true);
    if (srat == NULL) return false;

    srat = (srat_table_header_t *)map_region(acpi_config.srat_addr, srat->h.length, true);
    if (srat == NULL) return false;

    if (acpi_checksum(srat, srat->h.length) != 0) {
        return false;
    }
    // A table which contains fewer bytes than header + 1 processor local APIC entry + 1 memory affinity entry would be very weird.
    if (srat->h.length < sizeof(*srat) + sizeof(srat_processor_lapic_affinity_entry_t) + sizeof(srat_memory_affinity_entry_t)) {
        return false;
    }

    tab_entry_ptr = (uint8_t *)srat + sizeof(*srat);
    uint8_t * srat_table_end = (uint8_t *)srat + srat->h.length;
    // Pass 1: parse memory affinity entries and allocate proximity domains for each of them, while validating input a little bit.
    while (tab_entry_ptr < srat_table_end) {
        srat_entry_header_t *entry_header = (srat_entry_header_t *)tab_entry_ptr;
        if (entry_header->type == SRAT_PROCESSOR_APIC_AFFINITY) {
            if (entry_header->length != sizeof(srat_processor_lapic_affinity_entry_t)) {
                return false;
            }
        }
        else if (entry_header->type == SRAT_MEMORY_AFFINITY) {
            if (entry_header->length != sizeof(srat_memory_affinity_entry_t)) {
                return false;
            }
            srat_memory_affinity_entry_t *entry = (srat_memory_affinity_entry_t *)tab_entry_ptr;
            if (entry->flags & SRAT_MAF_ENABLED) {
                uint32_t proximity_domain = entry->proximity_domain;
                uint64_t start = entry->base_address;
                uint64_t end = entry->base_address + entry->address_length;
                int found = -1;

                if (start > end) {
                    // We've found a wraparound, that's not good.
                    return false;
                }

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
                        // TODO Display message ?
                        return false;
                    }
                }

                // Now that we have the index of the entry in proximity_domains in found, use it.
                if (num_memory_affinity_ranges < (int)(ARRAY_SIZE(memory_affinity_ranges))) {
                    memory_affinity_ranges[num_memory_affinity_ranges].proximity_domain_idx = (uint32_t)found;
                    memory_affinity_ranges[num_memory_affinity_ranges].start = start;
                    memory_affinity_ranges[num_memory_affinity_ranges].end = end;

                    num_memory_affinity_ranges++;
                } else {
                    // TODO Display message ?
                    return false;
                }
            }
        }
        else if (entry_header->type == SRAT_PROCESSOR_X2APIC_AFFINITY) {
            if (entry_header->length != sizeof(srat_processor_lx2apic_affinity_entry_t)) {
                return false;
            }
        } else {
            return false;
        }
        tab_entry_ptr += entry_header->length;
    }

    tab_entry_ptr = (uint8_t *)srat + sizeof(*srat);
    // Pass 2: parse processor APIC / x2APIC affinity entries.
    while (tab_entry_ptr < srat_table_end) {
        srat_entry_header_t *entry_header = (srat_entry_header_t *)tab_entry_ptr;
        uint32_t proximity_domain;
        uint32_t apic_id;
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
                    // We've found an affinity entry whose proximity domain we don't know about.
                    return false;
                }

                // Do we know about that APIC ID ?
                int found2 = -1;
                for (int i = 0; i < num_available_cpus; i++) {
                    if ((uint32_t)cpu_num_to_apic_id[i] == apic_id) {
                        found2 = i;
                        break;
                    }
                }

                if (found2 == -1) {
                    // We've found an affinity entry whose APIC ID we don't know about.
                    return false;
                }

                apic_id_to_proximity_domain_idx[apic_id] = (uint32_t)found1;
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
        tab_entry_ptr += entry_header->length;
    }

    // TODO sort on proximity address, like in pm_map.

    return true;
}

uint8_t checkout_max_memory_bits_of_this_numa_node(uint8_t range)
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

void map_the_numa_memory_range(uint8_t highest_bit)
{
    uint8_t i, node_nu;
    uint8_t node_offset = 44;

    //
    // First step, map the pm_map.
    //
    for (i = 0; i < pm_map_size; i++) {
        node_nu = (pm_map[i].start >> 32) & 0xF;
        if (node_nu != 0) {
            pm_map[i].start &= ~((uint64_t)0xF << (node_offset - PAGE_SHIFT));
            pm_map[i].start |= node_nu << (highest_bit - PAGE_SHIFT);

            pm_map[i].end &= ~((uint64_t)0xF << (node_offset - PAGE_SHIFT));
            pm_map[i].end |= node_nu << (highest_bit - PAGE_SHIFT);
        }
    }

    //
    // Second step, map the memory_affinity_ranges.
    //
    for (i = 0; i < num_memory_affinity_ranges; i++) {
        if (memory_affinity_ranges[i].proximity_domain_idx != 0) {
            node_nu = (memory_affinity_ranges[i].start >> node_offset) & 0xF;
            if (node_nu != 0) {
                memory_affinity_ranges[i].start &= ~((uint64_t)0xF << node_offset);
                memory_affinity_ranges[i].start |= node_nu << highest_bit;
                memory_affinity_ranges[i].end   &= ~((uint64_t)0xF << node_offset);
                memory_affinity_ranges[i].end   |= node_nu << highest_bit;
            }
        }
    }
}

void check_if_needs_to_map(void)
{
    uint8_t  i, local_memory_area_bits;
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
        if (max_memory_bits > 0) {
            map_numa_memory_range = true;
            highest_map_bit = max_memory_bits + 1;
            map_the_numa_memory_range(highest_map_bit);
        }
    }
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void smp_init(bool smp_enable)
{
    for (int i = 0; i < MAX_APIC_IDS; i++) {
        apic_id_to_cpu_num[i] = 0;
    }

    for (int i = 0; i < MAX_CPUS; i++) {
        cpu_num_to_apic_id[i] = 0;
    }

    num_available_cpus = 1;

    if (smp_enable) {
        find_cpus_in_madt();
    }

    for (int i = 0; i < num_available_cpus; i++) {
        apic_id_to_cpu_num[cpu_num_to_apic_id[i]] = i;
    }

    if (smp_enable) {
        if (find_numa_nodes_in_srat()) {
            check_if_needs_to_map();
        } else {
            // Do nothing.
        }
    }

    for (int i = 0; i < num_available_cpus; i++) {
        uint32_t proximity_domain_idx = apic_id_to_proximity_domain_idx[i];
        cpus_in_proximity_domain[proximity_domain_idx]++;
    }

    // Allocate a page of low memory for AP trampoline and sync objects.
    // These need to remain pinned in place during relocation.
    smp_heap_page = heap_alloc(HEAP_TYPE_LM_1, PAGE_SIZE, PAGE_SIZE) >> PAGE_SHIFT;

    ap_startup_addr = (uintptr_t)startup64;
    alloc_addr = HEAP_BASE_ADDR;
}

int smp_start(cpu_state_t cpu_state[MAX_CPUS])
{
    int cpu_num;

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
    //
    // MP sync the PMCNT with AP
    //
    __csrxchg_d(1 << 16, (1 << 16 | 0x3FF), 0x200);
    __csrwr_d(0x0, 0x201);

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

void smp_send_nmi(int cpu_num)
{
    send_ipi(cpu_num_to_apic_id[cpu_num], APIC_DELMODE_NMI, 0);
}

int smp_my_cpu_num(void)
{
    return num_available_cpus > 1 ? apic_id_to_cpu_num[my_apic_id()] : 0;
}

uint32_t smp_get_proximity_domain_idx(int cpu_num)
{
    return num_available_cpus > 1 ? apic_id_to_proximity_domain_idx[cpu_num_to_apic_id[cpu_num]] : 0;
}

int smp_narrow_to_proximity_domain(uint64_t start, uint64_t end, uint32_t * proximity_domain_idx, uint64_t * new_start, uint64_t * new_end)
{
    for (int i = 0; i < num_memory_affinity_ranges; i++) {
        uint64_t range_start = memory_affinity_ranges[i].start;
        uint64_t range_end = memory_affinity_ranges[i].end;

        if (start >= range_start) {
            if (start < range_end) {
                if (end <= range_end) {
                    // range_start start end range_end.
                    // The given vm_map range is entirely within a single memory affinity range. Nothing to split.
                    *proximity_domain_idx = memory_affinity_ranges[i].proximity_domain_idx;
                    *new_start = start;
                    *new_end = end;
                    return 1;
                } else {
                    // range_start start range_end end.
                    // The given vm_map range needs to be shortened.
                    *proximity_domain_idx = memory_affinity_ranges[i].proximity_domain_idx;
                    *new_start = start;
                    *new_end = range_end;
                    return 1;
                }
            } else {
                // range_start range_end start end
                // Do nothing, skip to next memory affinity range.
            }
        } else {
            if (end < range_start) {
                // start end range_start range_end.
                // Do nothing, skip to next memory affinity range.
            } else {
                if (end <= range_end) {
                    // start range_start end range_end.
                    *proximity_domain_idx = memory_affinity_ranges[i].proximity_domain_idx;
                    *new_start = start;
                    *new_end = range_start;
                    return 1;
                } else {
                    // start range_start range_end end.
                    *proximity_domain_idx = memory_affinity_ranges[i].proximity_domain_idx;
                    *new_start = start;
                    *new_end = range_start;
                    return 1;
                }
            }
        }
    }
    // If we come here, we haven't found a proximity domain which contains the given range. That shouldn't happen !
    return 0;
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
