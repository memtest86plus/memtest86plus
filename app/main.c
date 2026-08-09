// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from memtest86+ main.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, memtest@memtest.org
// https://www.memtest.org
// ------------------------------------------------
// main.c - MemTest-86  Version 3.5
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "assert.h"

#include "boot.h"
#include "bootparams.h"

#include "acpi.h"
#include "cache.h"
#include "cpuid.h"
#include "cpulocal.h"
#include "cpuinfo.h"
#include "heap.h"
#include "hwctrl.h"
#include "hwquirks.h"
#include "io.h"
#include "keyboard.h"
#include "pmem.h"
#include "memctrl.h"
#include "memsize.h"
#include "pci.h"
#include "screen.h"
#include "serial.h"
#include "simd.h"
#include "smbios.h"
#include "smp.h"
#include "temperature.h"
#include "timers.h"
#include "vmem.h"

#include "unistd.h"

#include "badram.h"
#include "config.h"
#include "display.h"
#include "error.h"
#include "reports.h"
#include "test.h"


#include "tests.h"

#include "tsc.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#ifndef TRACE_BARRIERS
#define TRACE_BARRIERS      0
#endif

#ifndef TEST_INTERRUPT
#define TEST_INTERRUPT      0
#endif

#if defined(__aarch64__)
// RAM may start well above physical address 0, so the load limits are
// computed at run time, relative to the start of RAM (check global_init)
#define LOW_LOAD_LIMIT      low_load_limit
#define HIGH_LOAD_LIMIT     high_load_limit
#else
#define LOW_LOAD_LIMIT      SIZE_C(4,MB)  // must be a multiple of the page size

#define HIGH_LOAD_LIMIT     (VM_PINNED_SIZE << PAGE_SHIFT)
#endif

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

// The following variables are written by the current "master" CPU, but may
// be read by all active CPUs.

static volatile int     init_state = 0;

static uintptr_t        low_load_addr;
static uintptr_t        high_load_addr;

static barrier_t        *start_barrier = NULL;

static bool             start_run  = false;
static bool             start_pass = false;
static bool             start_test = false;
static bool             rerun_test = false;

static bool dummy_run  = false;

static int              test_stage = 0;

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

// These are exposed in test.h.

uint16_t     chunk_index[MAX_CPUS];

int         num_enabled_cpus = 1;

barrier_t   *run_barrier = NULL;

spinlock_t  *error_mutex = NULL;

vm_map_t    vm_map[VMEM_MAX_CONTEXTS][MAX_MEM_SEGMENTS];
int         vm_map_size[VMEM_MAX_CONTEXTS];
size_t      num_mapped_pages[VMEM_MAX_CONTEXTS];

test_context_t test_contexts[VMEM_MAX_CONTEXTS] = { 0 };

// Per-context barriers, allocated from the pinned synchronization arena.
static barrier_t *context_barrier = NULL;

int         pass_num = 0;
int         test_num = 0;

bool        restart = false;
bool        bail    = false;

uintptr_t   test_addr[MAX_CPUS];

//------------------------------------------------------------------------------
// NUMA_PAR scheduling state
//------------------------------------------------------------------------------

// Terminal context statuses; transitions are monotonic within a binding
// epoch and only CPU 0 reinitializes status after global quiescence.
enum {
    CONTEXT_OK = 0,
    CONTEXT_CANCELLED,
    CONTEXT_MAP_FAILED,
    CONTEXT_MAP_OVERFLOW,
    CONTEXT_TOPOLOGY_FAILED
};

#define CONTEXT_NONE (-1)

// The set of CPUs that successfully entered the main control loop; fixed
// after AP startup, immutable for the run.
static bool cpu_is_global_participant[MAX_CPUS];

// The immutable per-run CPU selection snapshot; the UI edits
// pending_cpu_selected[] (packed in cpu_state for now) and CPU 0 copies it
// here at run boundaries.
static bool run_cpu_selected[MAX_CPUS];

// The CPUs that enter test-internal barriers in the current wave (all team
// CPUs for a parallel test, only the context master for a sequential one).
bool        cpu_is_test_participant[MAX_CPUS];

// The execution context bound to each CPU for the current wave; parked CPUs
// use context 0 only for identity-mapped control flow.
static uint8_t cpu_execution_context[MAX_CPUS];

// The dense per-context chunk index, rebuilt at every wave binding.
uint16_t    test_team_chunk_index[MAX_CPUS];

// CPU-backed memory domains that need an execution team, BSP first; their
// memory is owned by exactly one context in exactly one wave.
static uint32_t cpu_memory_domains[MAX_PROXIMITY_DOMAINS];
static unsigned int num_cpu_memory_domains = 0;

// Enabled (run_cpu_selected) CPUs per proximity domain.
static uint16_t enabled_cpus_in_proximity_domain[MAX_PROXIMITY_DOMAINS];

// Wave binding: domain -> context (or CONTEXT_NONE), context -> domain, and
// the execution owner of each domain's memory in the current wave.
static int8_t domain_to_context[MAX_PROXIMITY_DOMAINS];
static uint8_t context_to_domain[VMEM_MAX_CONTEXTS];
static int8_t memory_owner_context[MAX_PROXIMITY_DOMAINS];

static unsigned int bsp_proximity_domain = 0;

// Clamps cpu_execution_context[] to context 0 on the first boot, before BSS
// has been cleared: initialized data survives the early startup reads.
static unsigned int num_bound_execution_contexts = 1;

// True while a NUMA_PAR run is active (real run, available mapping model,
// at least two selected CPU-backed memory domains).
bool        numa_run_active = false;


//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

#define SHORT_BARRIER \
    if (TRACE_BARRIERS) { \
        trace(my_cpu, "Start barrier wait at %s line %i", __FILE__, __LINE__); \
    } \
    if (power_save < POWER_SAVE_HIGH) { \
        barrier_spin_wait(start_barrier); \
    } else { \
        barrier_halt_wait(start_barrier); \
    }

#define LONG_BARRIER \
    if (TRACE_BARRIERS) { \
        trace(my_cpu, "Start barrier wait at %s line %i", __FILE__, __LINE__); \
    } \
    if (power_save > POWER_SAVE_OFF) { \
        barrier_halt_wait(start_barrier); \
    } else { \
        barrier_spin_wait(start_barrier); \
    }

// The scheduler helpers defined after the state machine below (they depend
// on the window functions above them).
static bool numa_window_step(int my_cpu, int context, int iterations);

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// Records a terminal context status; the first terminal reason wins.
static void context_record_failure(int context, int reason)
{
    int expected = CONTEXT_OK;
    __atomic_compare_exchange_n(&test_contexts[context].status, &expected, reason,
                                false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

static int load_test_status(int context)
{
    return __atomic_load_n(&test_contexts[context].status, __ATOMIC_ACQUIRE);
}



static void run_at(uintptr_t addr, int my_cpu)
{
    uintptr_t *new_start_addr = (uintptr_t *)(addr + startup - _start);

    if (my_cpu == 0) {
        // Copy the program code and all data except the stacks.
        memmove((void *)addr, (void *)_start, _stacks - _start);
        // Copy the thread-local storage.
        size_t locals_offset = _stacks - _start + BSP_STACK_SIZE - LOCALS_SIZE;
        for (int cpu_num = 0; cpu_num < num_available_cpus; cpu_num++) {
            memcpy((void *)(addr + locals_offset), (void *)(_start + locals_offset), LOCALS_SIZE);
            locals_offset += AP_STACK_SIZE;
        }
#if defined(__aarch64__)
        // Make the copied code visible to instruction fetch.
        cache_sync_code_range((void *)addr, (void *)(addr + (_stacks - _start)));
#endif
    }
    LONG_BARRIER;

    // Jump to new_start_addr.
#ifdef __i386__
    // The 32-bit startup code needs to know where it is located.
    __asm__ __volatile__("movl %0, %%edi; jmp *%0" : : "r" (new_start_addr));
    __builtin_unreachable();
#elif defined(__aarch64__)
    // Discard any instructions speculatively fetched before the I-cache invalidation.
    __asm__ __volatile__("isb");
    ((void (*)(void))new_start_addr)();
#else
    ((void (*)(void))new_start_addr)(); // Formerly a non-portable construct: goto *new_start_addr;
#endif
}

// A BSP-only version of run_at(), used before the APs have been started. The
// thread-local storage is zeroed, as its original copy may not be backed by RAM.
static void relocate_to(uintptr_t addr)
{
    uintptr_t *new_start_addr = (uintptr_t *)(addr + startup - _start);

    // Copy the program code and all data except the stacks.
    memmove((void *)addr, (void *)_start, _stacks - _start);
    // Zero the thread-local storage.
    size_t locals_offset = _stacks - _start + BSP_STACK_SIZE - LOCALS_SIZE;
    for (int cpu_num = 0; cpu_num < num_available_cpus; cpu_num++) {
        memset((void *)(addr + locals_offset), 0, LOCALS_SIZE);
        locals_offset += AP_STACK_SIZE;
    }
#if defined(__aarch64__)
    // Make the copied code visible to instruction fetch.
    cache_sync_code_range((void *)addr, (void *)(addr + (_stacks - _start)));
#endif

    // Jump to new_start_addr.
#ifdef __i386__
    // The 32-bit startup code needs to know where it is located.
    __asm__ __volatile__("movl %0, %%edi; jmp *%0" : : "r" (new_start_addr));
    __builtin_unreachable();
#elif defined(__aarch64__)
    // Discard any instructions speculatively fetched before the I-cache invalidation.
    __asm__ __volatile__("isb");
    ((void (*)(void))new_start_addr)();
#else
    ((void (*)(void))new_start_addr)();
#endif
}

// Checks that the given address range lies entirely within a single region of
// usable RAM (the BIOS bootloader may have loaded us straddling the VGA/ROM hole).
static bool addr_range_is_usable(uintptr_t start, size_t size)
{
    for (int i = 0; i < pm_map_size; i++) {
        uintptr_t region_start = pm_map[i].start << PAGE_SHIFT;
        uintptr_t region_end   = pm_map[i].end   << PAGE_SHIFT;
        if (start >= region_start && (start + size) <= region_end) {
            return true;
        }
    }
    return false;
}

static bool set_load_addr(uintptr_t *load_addr, size_t program_size, uintptr_t lower_limit, uintptr_t upper_limit)
{
    uintptr_t current_start = (uintptr_t)_start;
    if (current_start >= lower_limit && (current_start + program_size) <= upper_limit
        && addr_range_is_usable(current_start, program_size)) {
        *load_addr = current_start;
        return true;
    }

    for (int i = 0; i < pm_map_size; i++) {
        uintptr_t try_start = pm_map[i].start << PAGE_SHIFT;
        uintptr_t try_limit = pm_map[i].end   << PAGE_SHIFT;
        if (try_start < lower_limit) try_start = lower_limit;
        uintptr_t try_end   = try_start + program_size;
        if (try_end > try_limit) continue;

        if (try_start >= upper_limit) break;
        if (try_end   <  lower_limit) continue;

        *load_addr = try_start;
        return true;
    }

    enable_trace = true;
    trace(0, "Insufficient free space in range 0x%x to 0x%x", lower_limit, upper_limit - 1);
    return false;
}

static void global_init(void)
{
    // Set once initialisation is complete; the early relocation below
    // restarts the program and re-enters here.
    static bool init_complete = false;
    if (init_complete) {
        return;
    }

    floppy_off();

    cpuid_init();

    simd_init();

    test_list_init();

    // Nothing before this should access the boot parameters, in case they are located above 4GB.
    // This is the first region we map, so it is guaranteed not to fail.
    boot_params_addr = map_region(boot_params_addr, sizeof(boot_params_t), true);

    hwctrl_init();

    screen_init();

    cpuinfo_init();

    pmem_init();

    heap_init();

    pci_init();

    quirks_init();

    acpi_init();

    timers_init();

    membw_init();

    smbios_init();

    badram_init();

    config_init();

    memctrl_init();

    tty_init();

    serial_log_start();

    smp_init(smp_enabled);

    // Force disable the NUMA code paths when no proximity domain was found.
    if (VMEM_MAX_CONTEXTS > 1 && num_proximity_domains == 0) {
        numa_mode = NUMA_OFF;
    }
    if (VMEM_MAX_CONTEXTS > 1 && smp_topology_too_large) {
        trace(0, "WARNING: SRAT declares more than %i proximity domains; NUMA disabled", MAX_PROXIMITY_DOMAINS);
    }
    if (VMEM_MAX_CONTEXTS > 1 && numa_mode == NUMA_PAR && (VMEM_MAX_CONTEXTS <= 1 || num_proximity_domains < 2)) {
        // NUMA_PAR needs independent CPU-backed memory teams; fall back to
        // legacy NUMA-aware placement with a clear message.
        trace(0, "NUMA_PAR unavailable (need 2+ memory domains); using NUMA_ON");
        numa_mode = NUMA_ON;
    }

    // Prepare the architecture mapping model for up to VMEM_MAX_CONTEXTS
    // execution contexts before the APs start. On x86-64 this initializes
    // every per-context PML4/PDP/PD2 root; i586 keeps a single context.
    if (VMEM_MAX_CONTEXTS > 1
        && !vmem_prepare_execution_contexts(VMEM_MAX_CONTEXTS)
        && numa_mode == NUMA_PAR) {
        numa_mode = NUMA_ON;
    }

    // At this point we have started reserving physical pages in the memory
    // map for data structures that need to be permanently pinned in place.
    // This may overwrite any data structures passed to us by the BIOS and/or
    // boot loader, e.g. the boot parameters, boot command line, and ACPI
    // tables. So do not access those data structures after this point.

    keyboard_init();

    display_init();

    error_init();

    cpu_temp_init();

    initial_config();

    clear_message_area();

    if (!smp_enabled) {
        num_available_cpus = 1;
    }

    num_enabled_cpus = 0;
    for (int i = 0; i < num_available_cpus; i++) {
        if (cpu_state[i] == CPU_STATE_ENABLED) {
            // NUMA-aware chunk indexes only for the legacy NUMA_ON mode; in
            // NUMA_PAR the dense per-context indexes are assigned at wave
            // binding, and when NUMA_PAR is requested but unavailable the
            // topology-agnostic path needs the global ordinals.
            if (VMEM_MAX_CONTEXTS > 1 && numa_mode == NUMA_ON) {
                uint32_t proximity_domain_idx = smp_get_proximity_domain_idx(i);
                chunk_index[i] = smp_alloc_cpu_in_proximity_domain(proximity_domain_idx);
            } else {
                chunk_index[i] = num_enabled_cpus;
            }
            num_enabled_cpus++;
        }
    }
    display_cpu_topology();

    master_cpu = 0;

    display_temperature();

    if (enable_trace) {
        display_pinned_message(0, 0,"CPU Trace");
        display_pinned_message(1, 0,"----  --------------------------------------------------------------------------");
        set_scroll_lock(true);
    } else if (enable_sm) {
        post_display_init();
    }

    size_t program_size = (_stacks - _start) + BSP_STACK_SIZE + (num_available_cpus - 1) * AP_STACK_SIZE;

    bool load_addr_ok =    set_load_addr( &low_load_addr, program_size,         0x1000,  LOW_LOAD_LIMIT)
                        && set_load_addr(&high_load_addr, program_size, LOW_LOAD_LIMIT, HIGH_LOAD_LIMIT);

    trace(0, "program size %ikB", (int)(program_size / 1024));
    trace(0, " low_load_addr %0*x", 2*sizeof(uintptr_t),  low_load_addr);
    trace(0, "high_load_addr %0*x", 2*sizeof(uintptr_t), high_load_addr);
    for (int i = 0; i < pm_map_size; i++) {
        trace(0, "pm %0*x - %0*x", 2*sizeof(uintptr_t), pm_map[i].start, 2*sizeof(uintptr_t), pm_map[i].end);
    }
    if (paging_incomplete) {
        trace(0, "WARNING: page table pool exhausted, some address ranges are not mapped");
    }
    if (acpi_config.rsdp_addr != 0) {
        trace(0, "ACPI RSDP (v%u.%u) found in %s at %0*x", acpi_config.ver_maj, acpi_config.ver_min, rsdp_source, 2*sizeof(uintptr_t), acpi_config.rsdp_addr);
        trace(0, "ACPI FADT found at %0*x", 2*sizeof(uintptr_t), acpi_config.fadt_addr);
        trace(0, "ACPI SRAT found at %0*x", 2*sizeof(uintptr_t), acpi_config.srat_addr);
        //trace(0, "ACPI SLIT found at %0*x", 2*sizeof(uintptr_t), acpi_config.slit_addr);
    }

    if (!load_addr_ok) {
        trace(0, "Cannot relocate program. Press any key to reboot...");
        while (get_key() == 0) { }
        reboot();
    }

    start_barrier = smp_alloc_barriers(1, 1);
    run_barrier   = smp_alloc_barriers(1, 1);
    context_barrier = smp_alloc_barriers(VMEM_MAX_CONTEXTS, 1);

    error_mutex   = smp_alloc_mutex();

    if (start_barrier == NULL || run_barrier == NULL
        || context_barrier == NULL || error_mutex == NULL) {
        // The pinned synchronization arena is exhausted; do not run without
        // synchronization objects.
        display_notice("Insufficient pinned memory for synchronization objects. Rebooting...");
        while (get_key() == 0) { }
        reboot();
    }

    start_run = true;
    dummy_run = true;
    restart = false;

    init_complete = true;

    // If the bootloader placed us so that the stack area extends beyond usable RAM,
    // move before the APs start; avoid a target that overlaps the running code.
    uintptr_t current_start = (uintptr_t)_start;
    if (!addr_range_is_usable(current_start, program_size)) {
        uintptr_t target = low_load_addr;
        if (target < (current_start + program_size) && current_start < (target + program_size)) {
            target = high_load_addr;
        }
        trace(0, "relocating to %0*x before starting CPUs", 2*sizeof(uintptr_t), target);
        relocate_to(target);
    }
}

static void ap_enumerate(int my_cpu)
{
    if (!cpuid_info.topology.is_hybrid) {
        return;
    }

    hybrid_core_type[my_cpu] = get_ap_hybrid_type();

    if (hybrid_core_type[my_cpu] == CORE_PCORE) {
        cpuid_info.topology.pcore_count++;
    } else if (hybrid_core_type[my_cpu] == CORE_ECORE) {
        cpuid_info.topology.ecore_count++;
    }

    if (hybrid_core_type[my_cpu] == CORE_ECORE && exclude_ecores) {
        cpu_state[my_cpu] = CPU_STATE_DISABLED;
        //TODO : hlt AP?
    }

    if (my_cpu == num_enabled_cpus - 1) {
        display_cpu_topology();
    }
}

static void setup_vm_map(int context, uintptr_t win_start, uintptr_t win_end)
{
    vm_map_size[context] = 0;

    num_mapped_pages[context] = 0;

    // Reduce the window to fit in the user-specified limits.
    if (win_start < pm_limit_lower) {
        win_start = pm_limit_lower;
    }
    if (win_end > pm_limit_upper) {
        win_end = pm_limit_upper;
    }
    if (win_start >= win_end) {
        return;
    }

    // Now initialise the virtual memory map with the intersection
    // of the window and the physical memory segments.
    for (int i = 0; i < pm_map_size; i++) {
        // These are page numbers.
        uintptr_t seg_start = pm_map[i].start;
        uintptr_t seg_end   = pm_map[i].end;
        if (seg_start <= win_start) {
            seg_start = win_start;
        }
        if (seg_end >= win_end) {
            seg_end = win_end;
        }
        if (seg_start < seg_end && seg_start < win_end && seg_end > win_start) {
            // We need to test part of that physical memory segment.
            uint64_t orig_start;
            uint64_t orig_end;
            uint32_t proximity_domain_idx;
            uint64_t new_start;
            uint64_t new_end;

            if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
                // NUMA_PAR: split by physical-memory proximity domain and
                // keep only the spans owned by this team.
                orig_start = (uint64_t)seg_start << PAGE_SHIFT;
                orig_end = (uint64_t)seg_end << PAGE_SHIFT;

                while (1) {
                    if (orig_start >= orig_end) {
                        // The window's remaining range is exhausted, even
                        // when its last span belongs to another team.
                        break;
                    }
                    if (vm_map_size[context] >= MAX_MEM_SEGMENTS) {
                        context_record_failure(context, CONTEXT_MAP_OVERFLOW);
                        break;
                    }
                    if (smp_narrow_to_proximity_domain(orig_start, orig_end, &proximity_domain_idx, &new_start, &new_end)) {
                        if (new_end <= orig_start) {
                            // Defensive progress check: malformed topology
                            // data must not make this loop process the same
                            // span forever.
                            context_record_failure(context, CONTEXT_TOPOLOGY_FAILED);
                            break;
                        }
                        if (memory_owner_context[proximity_domain_idx] != context) {
                            // Not owned by this team: skip to the next span.
                            orig_start = new_end;
                            continue;
                        }
                        // Create a new entry in the virtual memory map.
                        num_mapped_pages[context] += (new_end - new_start) >> PAGE_SHIFT;
                        vm_map[context][vm_map_size[context]].pm_base_addr = new_start >> PAGE_SHIFT;
                        vm_map[context][vm_map_size[context]].start        = first_word_mapping(new_start >> PAGE_SHIFT);
                        vm_map[context][vm_map_size[context]].end          = last_word_mapping((new_end >> PAGE_SHIFT) - 1, sizeof(testword_t));
                        vm_map[context][vm_map_size[context]].proximity_domain_idx = proximity_domain_idx;
                        vm_map_size[context]++;
                        if (new_start != orig_start || new_end != orig_end) {
                            // Proceed to the next part of the range.
                            orig_start = new_end; // No shift here, we already have a physical address.
                            orig_end = (uint64_t)seg_end << PAGE_SHIFT;
                        } else {
                            // We're done with this range.
                            break;
                        }
                    } else {
                        // An uncovered gap or ambiguous ownership would
                        // let two contexts alias the same span.
                        context_record_failure(context, CONTEXT_TOPOLOGY_FAILED);
                        break;
                    }
                }
            } else if (VMEM_MAX_CONTEXTS > 1 && numa_mode == NUMA_ON) {
                // Legacy NUMA_ON: split by proximity domain for NUMA-aware
                // chunk placement. A requested-but-inactive NUMA_PAR runs
                // the topology-agnostic path below.
                orig_start = (uint64_t)seg_start << PAGE_SHIFT;
                orig_end = (uint64_t)seg_end << PAGE_SHIFT;

                while (1) {
                    if (vm_map_size[context] >= MAX_MEM_SEGMENTS) {
                        break;
                    }
                    if (smp_narrow_to_proximity_domain(orig_start, orig_end, &proximity_domain_idx, &new_start, &new_end)) {
                        // Create a new entry in the virtual memory map.
                        num_mapped_pages[context] += (new_end - new_start) >> PAGE_SHIFT;
                        vm_map[context][vm_map_size[context]].pm_base_addr = new_start >> PAGE_SHIFT;
                        vm_map[context][vm_map_size[context]].start        = first_word_mapping(new_start >> PAGE_SHIFT);
                        vm_map[context][vm_map_size[context]].end          = last_word_mapping((new_end >> PAGE_SHIFT) - 1, sizeof(testword_t));
                        vm_map[context][vm_map_size[context]].proximity_domain_idx = proximity_domain_idx;
                        vm_map_size[context]++;
                        if (new_start != orig_start || new_end != orig_end) {
                            // Proceed to the next part of the range.
                            orig_start = new_end; // No shift here, we already have a physical address.
                            orig_end = (uint64_t)seg_end << PAGE_SHIFT;
                        } else {
                            // We're done with this range.
                            break;
                        }
                    } else {
                        // Could not match with proximity domain, fall back to default behaviour. This shouldn't happen !
                        vm_map[context][vm_map_size[context]].proximity_domain_idx = 0;
                        goto non_numa_vm_map_entry;
                    }
                }
            } else {
non_numa_vm_map_entry:
                num_mapped_pages[context] += seg_end - seg_start;
                vm_map[context][vm_map_size[context]].pm_base_addr = seg_start;
                vm_map[context][vm_map_size[context]].start        = first_word_mapping(seg_start);
                vm_map[context][vm_map_size[context]].end          = last_word_mapping(seg_end - 1, sizeof(testword_t));
                vm_map_size[context]++;
            }
        }
    }
#if 0
    for (int i = 0; i < vm_map_size[context]; i++) {
        do_trace(0, "vm %0*x - %0*x", 2*sizeof(uintptr_t), vm_map[context][i].start, 2*sizeof(uintptr_t), vm_map[context][i].end);
    }
#endif
}

// The scheduler is a relocation-resumable state machine: run_at() never
// returns, so after every relocation main() calls test_all_windows() again,
// which resumes from the copied scheduler_phase/current_wave globals. Each
// logical test stage starts one state-machine sequence (wave 0..N-1 per
// stage); NUMA_PAR binds execution contexts to CPU-backed proximity domains
// at WAVE_BIND, while legacy modes run the single legacy context.
//
// Every reconfiguration (barrier_reset, context rebind, status reset,
// relocation) happens only between the LONG/SHORT_BARRIER pairs that bracket
// each phase, so no running barrier generation or context wait is ever
// reconfigured concurrently: the barriers are the quiescence protocol.

typedef enum {
    GLOBAL_STAGE_ONCE,   // test-specific stage run once by CPU 0 (e.g. bit-fade delay)
    WAVE_BIND,           // bind the wave: publish counts/barriers, then first window
    WAVE_WINDOW_0,       // test window 0 (below LOW_LOAD_LIMIT), relocated high
    WAVE_WINDOWS_1_PLUS, // test windows 1 and above, relocated low
    WAVE_FINISH,         // advance to the next wave, or finish the stage
    WAVE_DONE            // the only state that lets main() advance the stage
} test_scheduler_phase_t;

static test_scheduler_phase_t scheduler_phase;

// Copied global state: relocation re-entry resumes the wave sequence from
// exactly where it stopped. Legacy modes and the dummy run use one wave.
static unsigned int current_wave = 0;
static unsigned int num_execution_waves = 1;

// Runs the window currently selected by the context master. Every participant
// maps the same physical window and tests its own chunk of it. Returns false
// when the phase must be aborted (e.g. the mapping limit was reached).
static bool run_test_window(int my_cpu, bool i_am_master, bool i_am_active, bool dummy_run_active, int iterations)
{
    if (!i_am_active) {
        return true;
    }

    if (num_mapped_pages[0] == 0) {
        // No memory to test in this window.
        if (i_am_master) {
            window_num++;
        }
        return true;
    }

    if (dummy_run_active) {
        if (i_am_master) {
            ticks_per_test[pass_num][test_num] += run_test(-1, test_num, test_stage, iterations);
        }
        return true;
    }

    if (!map_window(0, vm_map[0][0].pm_base_addr)) {
        // Either there is no PAE or we are at the PAE limit.
        return false;
    }
    run_test(my_cpu, test_num, test_stage, iterations);

    if (i_am_master) {
        window_num++;
    }
    return true;
}

// The scheduler's window boundaries in pages: window 0 covers the low
// region [0, LOW_LOAD_LIMIT); window 1 ends at the next 1 GiB boundary
// (on aarch64 LOW_LOAD_LIMIT may be above VM_WINDOW_SIZE, so the end is
// rounded up to avoid rechecking the region containing the low copy);
// the following windows step by VM_WINDOW_SIZE. Every window is at most
// one tick block, which the work accounting relies on.
static void window_page_bounds(uint64_t window_index, uintptr_t *win_start, uintptr_t *win_end)
{
    uintptr_t low_limit = LOW_LOAD_LIMIT >> PAGE_SHIFT;
    // The end of window 1, in pages. VM_WINDOW_SIZE is in pages, so the
    // aarch64 rounding works on page numbers.
#if defined(__aarch64__)
    uintptr_t window_one_end = (low_limit + VM_WINDOW_SIZE)
                             & ~(VM_WINDOW_SIZE - 1);
#else
    uintptr_t window_one_end = VM_WINDOW_SIZE;
#endif

    switch (window_index) {
      case 0:
        *win_start = 0;
        *win_end   = low_limit;
        break;
      case 1:
        *win_start = low_limit;
        *win_end   = window_one_end;
        break;
      default:
        *win_start = window_one_end + (window_index - 2) * VM_WINDOW_SIZE;
        *win_end   = *win_start + VM_WINDOW_SIZE;
    }
}
#include "numa_par.c"


// NUMA_PAR: one window iteration of the calling CPU's context. The context
// master computes the window and the map; the context barrier publishes
// them. Returns false when the context must stop (terminal status); every
// participant rendezvouses at the barrier before exiting, so a terminal
// status never strands a team.
static bool numa_window_step(int my_cpu, int context, int iterations)
{
    test_context_t *ctx = &test_contexts[context];
    bool stop = false;

    if (my_cpu == ctx->master_cpu_num) {
        if (load_test_status(context) != CONTEXT_OK) {
            stop = true;
        } else {
            if (ctx->window_index <= 1) {
                window_page_bounds(ctx->window_index, &ctx->window_start, &ctx->window_end);
            } else {
                ctx->window_start = ctx->window_end;
                ctx->window_end  += VM_WINDOW_SIZE;
            }
            if (ctx->window_start >= pm_map[pm_map_size - 1].end) {
                // The window enumeration is exhausted. The sticky flag is
                // published by the context barrier below and never rewritten
                // during this binding, so every participant reads the same
                // end-of-enumeration decision (a per-window value would race
                // the master's next write).
                ctx->windows_exhausted = true;
            }
            setup_vm_map(context, ctx->window_start, ctx->window_end);
        }
    }
    barrier_spin_wait(test_run_barrier());

    if (stop || load_test_status(context) != CONTEXT_OK) {
        // Every team member evaluates the same published state after the
        // same barrier generation, so the whole context exits together; a
        // lone early exit would strand the others in the next tick barrier.
        return false;
    }
    if (ctx->windows_exhausted) {
        // No more windows for this context.
        return false;
    }
    if (num_mapped_pages[context] == 0) {
        // No memory to test in this window.
        if (my_cpu == ctx->master_cpu_num) {
            ctx->window_index++;
        }
        // Close the step with a second rendezvous: without it the master
        // would rewrite the next window's state while the peers are still
        // reading this step's windows_exhausted/num_mapped_pages after the
        // publication barrier above, and a peer could exit on a decision
        // for a window it never reached.
        barrier_spin_wait(test_run_barrier());
        return true;
    }

    if (!map_window(context, vm_map[context][0].pm_base_addr)) {
        // Either there is no PAE or we are at the PAE limit.
        context_record_failure(context, CONTEXT_MAP_FAILED);
    }
    barrier_spin_wait(test_run_barrier());

    if (load_test_status(context) != CONTEXT_OK) {
        return false;
    }
    run_test(my_cpu, test_num, test_stage, iterations);

    if (my_cpu == ctx->master_cpu_num) {
        ctx->window_index++;
    }
    return true;
}

// Window 0 covers [0, LOW_LOAD_LIMIT). The master computes the window and
// the map; the publication barrier makes them visible before test access.
static bool run_owned_window_zero(int my_cpu, bool i_am_master, bool i_am_active, int iterations)
{
    if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
        // The context master computes window 0 and its map; the context
        // barrier publishes them. Parked CPUs wait only at the phase-end
        // global barrier.
        if (cpu_is_test_participant[my_cpu]) {
            numa_window_step(my_cpu, execution_context_for_cpu(my_cpu), iterations);
        }
        return true;
    }

    if (i_am_master) {
        test_context_t *ctx = &test_contexts[0];
        window_page_bounds(0, &ctx->window_start, &ctx->window_end);
        setup_vm_map(0, ctx->window_start, ctx->window_end);
    }
    SHORT_BARRIER;

    return run_test_window(my_cpu, i_am_master, i_am_active, dummy_run, iterations);
}

// Windows 1 and above, in ascending order, until the context has enumerated
// its owned memory. In legacy modes this is the pre-existing window loop.
static void run_owned_windows_1_plus(int my_cpu, bool i_am_master, bool i_am_active, int iterations)
{
    if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
        if (!cpu_is_test_participant[my_cpu]) {
            return;
        }
        int context = execution_context_for_cpu(my_cpu);
        while (numa_window_step(my_cpu, context, iterations)) { }
        return;
    }

    bool running = true;
    do {
        LONG_BARRIER;
        if (bail) {
            break;
        }

        if (i_am_master) {
            test_context_t *ctx = &test_contexts[0];
            if (window_num == 1) {
                window_page_bounds(1, &ctx->window_start, &ctx->window_end);
            } else {
                ctx->window_start = ctx->window_end;
                ctx->window_end  += VM_WINDOW_SIZE;
            }
            setup_vm_map(0, ctx->window_start, ctx->window_end);
        }
        SHORT_BARRIER;

        running = run_test_window(my_cpu, i_am_master, i_am_active, dummy_run, iterations);
    } while (running && test_contexts[0].window_end < pm_map[pm_map_size - 1].end);
}

// Reaches a pre-copy global rendezvous, then relocates the whole program.
// run_at() performs its own post-copy barrier before the CPUs jump to the
// newly copied image; the resume phase is already published in the copied
// globals, so main() re-enters the same phase after the jump.
static void relocate_all_and_resume(uintptr_t addr, int my_cpu)
{
    LONG_BARRIER;
    run_at(addr, my_cpu);
    __builtin_unreachable();
}

// True when the selected map (pm_map intersected with the selected range)
// has any memory below LOW_LOAD_LIMIT, i.e. when window 0 must be executed.
// The decision is purely map-based: a decision based on the accepted SRAT
// ranges could skip window 0 when selected usable memory sits in an
// uncovered SRAT gap, silently omitting it, because setup_vm_map(), which
// would record CONTEXT_TOPOLOGY_FAILED and fall back to legacy mode, would
// never inspect the window.
static bool selected_map_has_window_zero(void)
{
    uintptr_t limit = MIN(pm_limit_upper, LOW_LOAD_LIMIT >> PAGE_SHIFT);
    for (int i = 0; i < pm_map_size; i++) {
        uintptr_t s = pm_map[i].start;
        uintptr_t e = pm_map[i].end;
        if (s < pm_limit_lower) {
            s = pm_limit_lower;
        }
        if (e > limit) {
            e = limit;
        }
        if (s < e) {
            return true;
        }
    }
    return false;
}

// Decides whether window 0 will be tested in this stage/wave. In NUMA_PAR
// every wave executes window 0 whenever the selected map has any memory
// below LOW_LOAD_LIMIT; a wave whose contexts map nothing there just skips
// the empty window (and performs one harmless extra relocation). In legacy
// modes the pre-existing window-0 avoidance rules (multi-stage tests and
// pm_limit_lower above LOW_LOAD_LIMIT) apply before any relocation decision
// is made.
static bool wave_has_window_zero(void)
{
    if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
        return selected_map_has_window_zero();
    }

    if (window_num == 0 && test_list[test_num].stages > 1) {
        // A multi-stage test runs through all the windows at each stage.
        // Relocation may disrupt the test.
        window_num = 1;
    }
    if (window_num == 0 && pm_limit_lower >= LOW_LOAD_LIMIT) {
        // Avoid unnecessary relocation.
        window_num = 1;
    }
    return window_num == 0;
}

static void test_all_windows(int my_cpu)
{
    bool parallel_test = false;
    bool i_am_master = (my_cpu == master_cpu);
    bool i_am_active = i_am_master;
    if (!dummy_run) {
        // In NUMA_PAR the test-pattern CPU mode decides: PAR patterns use
        // every CPU of each context team, ONE patterns use the fixed context
        // master, independent of the (hidden) legacy CPU sequencing mode.
        if (test_list[test_num].cpu_mode == PAR && ((VMEM_MAX_CONTEXTS > 1 && numa_run_active) || cpu_mode == PAR)) {
            parallel_test = true;
            i_am_active = true;
        }
    }

    int iterations = test_list[test_num].iterations;
    if (pass_num == 0) {
        // Reduce iterations for a faster first pass.
        iterations /= 3;
    }

    for (;;) {
        switch (scheduler_phase) {
          case GLOBAL_STAGE_ONCE:
            // A test-specific global-once stage (e.g. the bit-fade delay)
            // runs once on CPU 0 between memory-touching phases.
            LONG_BARRIER;
            if (my_cpu == 0) {
                scheduler_phase = WAVE_DONE;
            }
            LONG_BARRIER;
            continue;

          case WAVE_BIND:
            LONG_BARRIER;
            if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
                // CPU 0 is the binding and reset owner, also when parked.
                if (my_cpu == 0) {
                    bind_execution_wave(current_wave, parallel_test);
                    scheduler_phase = wave_has_window_zero() ? WAVE_WINDOW_0 : WAVE_WINDOWS_1_PLUS;
                }
            } else if (VMEM_MAX_CONTEXTS > 1 && i_am_master) {
                bind_execution_wave(0, parallel_test);
                scheduler_phase = wave_has_window_zero() ? WAVE_WINDOW_0 : WAVE_WINDOWS_1_PLUS;
            }
            SHORT_BARRIER;
            continue;

          case WAVE_WINDOW_0: {
            bool window_zero_ok;
            if (!dummy_run && (uintptr_t)&_start != high_load_addr) {
                relocate_all_and_resume(high_load_addr, my_cpu);
            }
            window_zero_ok = run_owned_window_zero(my_cpu, i_am_master, i_am_active, iterations);
            LONG_BARRIER;
            if (my_cpu == 0) {
                // A window-0 map failure (the PAE limit was reached) aborts
                // the stage: the following windows would fail identically,
                // and the run boundary falls back to legacy placement.
                scheduler_phase = window_zero_ok ? WAVE_WINDOWS_1_PLUS : WAVE_FINISH;
            }
            SHORT_BARRIER;
            continue;
          }

          case WAVE_WINDOWS_1_PLUS:
            if (!dummy_run && (uintptr_t)&_start != low_load_addr) {
                relocate_all_and_resume(low_load_addr, my_cpu);
            }
            run_owned_windows_1_plus(my_cpu, i_am_master, i_am_active, iterations);
            LONG_BARRIER;
            if (my_cpu == 0) {
                scheduler_phase = WAVE_FINISH;
            }
            SHORT_BARRIER;
            continue;

          case WAVE_FINISH:
            LONG_BARRIER;
            if (my_cpu == 0) {
                if (++current_wave < num_execution_waves) {
                    scheduler_phase = WAVE_BIND;
                } else {
                    scheduler_phase = WAVE_DONE;
                }
            }
            SHORT_BARRIER;
            continue;

          case WAVE_DONE:
            return;
        }
    }
}

static void select_next_master(void)
{
    do {
        master_cpu = (master_cpu + 1) % num_available_cpus;
    } while (cpu_state[master_cpu] == CPU_STATE_DISABLED);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

// The execution-context accessors. Legacy modes (and the BSP-only dummy run)
// always use context 0; the NUMA_PAR scheduler binds CPU ordinals to contexts
// at globally quiescent wave boundaries.

test_context_t *test_context(void)
{
    if (VMEM_MAX_CONTEXTS <= 1 || !numa_run_active) {
        return &test_contexts[0];
    }
    return &test_contexts[execution_context_for_cpu(smp_my_cpu_num())];
}

int test_context_index(void)
{
    if (VMEM_MAX_CONTEXTS <= 1 || !numa_run_active) {
        return 0;
    }
    return execution_context_for_cpu(smp_my_cpu_num());
}

int execution_context_for_cpu(int cpu)
{
    if (VMEM_MAX_CONTEXTS <= 1 || !numa_run_active) {
        return 0;
    }
    if (cpu < 0 || cpu >= MAX_CPUS) {
        return 0;
    }
    int context = cpu_execution_context[cpu];
    if (context < 0 || context >= (int)num_bound_execution_contexts || context >= VMEM_MAX_CONTEXTS) {
        // Unbound/invalid ordinals use the identity-mapped control context.
        // num_bound_execution_contexts is 1 in .data, so this clamp also
        // protects the first-boot startup reads before BSS has been cleared.
        return 0;
    }
    return context;
}

barrier_t *test_run_barrier(void)
{
    if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
        int cpu = smp_my_cpu_num();
        // A parked CPU (including one whose wave is not bound yet) must never
        // enter a test-internal barrier.
        assert(cpu_is_test_participant[cpu]);
        int context = execution_context_for_cpu(cpu);
        assert(context >= 0 && context < VMEM_MAX_CONTEXTS);
        assert(test_contexts[context].active_cpu_count > 0);
        return &context_barrier[context];
    }
    return run_barrier;
}



//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

// The main entry point called from the startup code.

void main(void)
{
    int my_cpu;
    if (init_state == 0) {
        // If this is the first time here, we must be CPU 0, as the APs haven't been started yet.
        my_cpu = 0;
    } else {
        my_cpu = smp_my_cpu_num();
    }
    if (init_state < 2) {
        cache_on();
        if (my_cpu == 0) {
            global_init();
            init_state = 1;
            if (enable_trace && num_enabled_cpus > 1) {
                set_scroll_lock(false);
                trace(0, "starting other CPUs");
            }
            barrier_reset(start_barrier, num_enabled_cpus);
            int failed = smp_start(cpu_state);
            if (failed) {
                const char *message = "Failed to start CPU core %i. Press any key to reboot...";
                display_notice_with_args(strlen(message), message, failed);
                while (get_key() == 0) { }
                reboot();
            }
            // The CPUs that successfully entered the main loop form the
            // immutable global control set for the run.
            for (int cpu = 0; cpu < num_available_cpus; cpu++) {
                cpu_is_global_participant[cpu] = (cpu_state[cpu] != CPU_STATE_DISABLED);
            }
            if (enable_trace && num_enabled_cpus > 1) {
                trace(0, "all other CPUs started");
                set_scroll_lock(true);
            }
            init_state = 2;
        } else {
            trace(my_cpu, "AP started");
            cpu_state[my_cpu] = CPU_STATE_RUNNING;
            ap_enumerate(my_cpu);
            while (init_state < 2) {
                usleep(100);
            }
        }
    }

#if TEST_INTERRUPT
    if (my_cpu == 0) {
        __asm__ __volatile__ ("int $1");
    }
#endif

    // Due to the need to relocate ourselves in the middle of tests, the following
    // code cannot be written in the natural way as a set of nested loops. So we
    // have a single loop and use global state variables to allow us to restart
    // where we left off after each relocation.

    while (1) {
        SHORT_BARRIER;
        if (my_cpu == 0) {
            if (start_run) {
                pass_num = 0;
                start_pass = true;
                if (!dummy_run) {
                    display_start_run();
                    badram_init();
                    error_init();
                    serial_log_run_start();
                }
                // Snapshot the CPU selection for this run and materialize
                // the domain metadata used by the NUMA_PAR scheduler.
                for (int cpu = 0; cpu < num_available_cpus; cpu++) {
                    run_cpu_selected[cpu] = (cpu_state[cpu] != CPU_STATE_DISABLED);
                }
                if (VMEM_MAX_CONTEXTS > 1) {
                    build_numa_domain_list();
                }
            }
            if (start_pass) {
                test_num = 0;
                start_test = true;
                if (dummy_run) {
                    ticks_per_pass[pass_num] = 0;
                } else {
                    display_start_pass();
                    serial_log_event(SLOG_PASS_START);
                }
            }
            if (start_test) {
                trace(my_cpu, "start test %i", test_num);
                test_stage = 0;
                rerun_test = true;
                if (dummy_run) {
                    ticks_per_test[pass_num][test_num] = 0;
                } else if (test_list[test_num].enabled) {
                    display_start_test();
                    serial_log_event(SLOG_TEST_START);
                }
                bail = false;
            }
            if (rerun_test) {
                // Start a new state-machine sequence for this logical
                // test/stage. This must not run on relocation re-entry,
                // or the scheduler would rebind and reset progress.
                current_wave = 0;
                num_execution_waves = 1;   // NUMA_PAR computes this from the domains
                scheduler_phase = WAVE_BIND;
                test_contexts[0].window_index = 0;
                test_contexts[0].window_start = 0;
                test_contexts[0].window_end   = 0;
            }
            start_run  = false;
            start_pass = false;
            start_test = false;
            rerun_test = false;
        }
        SHORT_BARRIER;
        if (test_list[test_num].enabled) {
            test_all_windows(my_cpu);
        }
        SHORT_BARRIER;
        if (my_cpu != 0) {
            continue;
        }

        check_input();
        if (restart) {
            // The configuration has been changed.
            master_cpu = 0;
            start_run = true;
            dummy_run = true;
            restart = false;
            continue;
        }
        error_update();

        if (test_list[test_num].enabled) {
            if (++test_stage < test_list[test_num].stages) {
                rerun_test = true;
                continue;
            }
            test_stage = 0;

            switch (cpu_mode) {
              case PAR:
                if (test_list[test_num].cpu_mode == SEQ) {
                    select_next_master();
                    if (master_cpu != 0) {
                        rerun_test = true;
                        continue;
                    }
                }
                break;
              case ONE:
                select_next_master();
                break;
              case SEQ:
                select_next_master();
                if (master_cpu != 0) {
                    rerun_test = true;
                    continue;
                }
                break;
              default:
                break;
            }
        }

        if (dummy_run) {
            ticks_per_pass[pass_num] += ticks_per_test[pass_num][test_num];
        } else if (test_list[test_num].enabled) {
            serial_log_event(SLOG_TEST_END);
        }

        start_test = true;
        test_num++;
        if (test_num < NUM_TEST_PATTERNS) {
            continue;
        }

        if (!dummy_run) {
            serial_log_event(SLOG_PASS_END);
        }
        pass_num++;
        if (dummy_run && pass_num == NUM_PASS_TYPES) {
            start_run = true;
            dummy_run = false;
            continue;
        }

        start_pass = true;
        if (!dummy_run) {
            display_pass_count(pass_num);
            if (error_count == 0) {
                display_status("Pass   ");
                display_big_status(true);
            } else {
                display_big_status(false);
            }
        }
    }
}
