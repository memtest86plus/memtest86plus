// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2022 Sam Demeulemeester.
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

#include "acpi.h"
#include "boot.h"
#include "bootparams.h"
#include "efi.h"

#include "cpuid.h"
#include "memrw32.h"
#include "memsize.h"
#include "msr.h"
#include "pmem.h"
#include "string.h"
#include "unistd.h"
#include "vmem.h"

#include "smp.h"

#define SEQUENTIAL_AP_START         0

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_APIC_IDS                256

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

#define MP_PROCESSOR                0
#define MP_BUS                      1
#define MP_IOAPIC                   2
#define MP_INTSRC                   3
#define MP_LINTSRC                  4

// MP processor cpu_flag values

#define CPU_ENABLED                 1
#define CPU_BOOTPROCESSOR           2

// MADT entry types

#define MADT_PROCESSOR              0
#define MADT_LAPIC_ADDR             5

// MADT processor flag values

#define MADT_PF_ENABLED             0x1
#define MADT_PF_ONLINE_CAPABLE      0x2

// Private memory heap used for AP trampoline and synchronisation objects

#define HEAP_BASE_ADDR              (smp_heap_page << PAGE_SHIFT)

#define AP_TRAMPOLINE_PAGE          (smp_heap_page)

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef volatile uint32_t apic_register_t[4];

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

typedef struct {
    uint8_t     type;
    uint8_t     length;
    uint8_t     acpi_id;
    uint8_t     apic_id;
    uint32_t    flags;
} madt_processor_entry_t;

typedef struct {
    uint8_t     type;
    uint8_t     length;
    uint16_t    reserved;
    uint64_t    lapic_addr;
} madt_lapic_addr_entry_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static apic_register_t  *apic = NULL;

static uint8_t          apic_id_to_cpu_num[MAX_APIC_IDS];

static uint8_t          cpu_num_to_apic_id[MAX_CPUS];

static uintptr_t        smp_heap_page = 0;

static uintptr_t        alloc_addr = 0;

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int num_available_cpus = 1;  // There is always at least one CPU, the BSP

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static int my_apic_id(void)
{
    return read32(&apic[APIC_REG_ID][0]) >> 24;
}

static void apic_write(int reg, uint32_t val)
{
    write32(&apic[reg][0], val);
}

static uint32_t apic_read(int reg)
{
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

static bool find_cpus_in_madt(void)
{
    if(madt_addr == 0) return false;

    madt_table_header_t *mpc = (madt_table_header_t *)map_region(madt_addr, sizeof(madt_table_header_t), true);
    if (mpc == NULL) return false;

    mpc = (madt_table_header_t *)map_region(madt_addr, mpc->length, true);
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
        if (entry_header->type == MADT_PROCESSOR) {
            madt_processor_entry_t *entry = (madt_processor_entry_t *)tab_entry_ptr;
            if (entry->flags & (MADT_PF_ENABLED|MADT_PF_ONLINE_CAPABLE)) {
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
        if (entry_header->type == MADT_LAPIC_ADDR) {
            madt_lapic_addr_entry_t *entry = (madt_lapic_addr_entry_t *)tab_entry_ptr;
            apic_addr = (uintptr_t)entry->lapic_addr;
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

static inline void send_ipi(int apic_id, int trigger, int level, int mode, uint8_t vector)
{
    apic_write(APIC_REG_ICRHI, apic_id << 24);

    apic_write(APIC_REG_ICRLO, trigger << 15 | level << 14 | mode << 8 | vector);
}

static bool send_ipi_and_wait(int apic_id, int trigger, int level, int mode, uint8_t vector, int delay_before_poll)
{
    send_ipi(apic_id, trigger, level, mode, vector);

    usleep(delay_before_poll);

    // Wait for send complete or timeout after 100ms.
    int timeout = 1000;
    while (timeout > 0) {
        bool send_pending = (apic_read(APIC_REG_ICRLO) & APIC_ICR_BUSY);
        if (!send_pending) {
            return true;
        }
        usleep(100);
        timeout--;
    }
    return false;
}

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

    if (cpuid_info.flags.x2apic) {
        uint32_t msrl, msrh;
        rdmsr(MSR_IA32_APIC_BASE, msrl, msrh);
        if ((msrl & IA32_APIC_ENABLED) && (msrl & IA32_APIC_EXTENDED)) {
            // We don't currently support x2APIC mode.
            smp_enable = false;
        }
    }

    if (smp_enable) {
        (void)(find_cpus_in_madt() || find_cpus_in_floating_mp_struct());

    }

    for (int i = 0; i < num_available_cpus; i++) {
        apic_id_to_cpu_num[cpu_num_to_apic_id[i]] = i;
    }

    // Reserve last page of first segment for AP trampoline and sync objects.
    // These need to remain pinned in place during relocation.
    smp_heap_page = --pm_map[0].end;

    ap_startup_addr = (uintptr_t)startup;

    size_t ap_trampoline_size = ap_trampoline_end - ap_trampoline;
    memcpy((uint8_t *)HEAP_BASE_ADDR, ap_trampoline, ap_trampoline_size);

    alloc_addr = HEAP_BASE_ADDR + ap_trampoline_size;
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
    while (apic_read(APIC_REG_ICRLO) & APIC_ICR_BUSY) {
        __builtin_ia32_pause();
    }
    send_ipi(cpu_num_to_apic_id[cpu_num], 0, 0, APIC_DELMODE_NMI, 0);
}

int smp_my_cpu_num(void)
{
    return num_available_cpus > 1 ? apic_id_to_cpu_num[my_apic_id()] : 0;
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
