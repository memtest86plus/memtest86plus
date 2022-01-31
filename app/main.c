// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from memtest86+ main.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// ------------------------------------------------
// main.c - MemTest-86  Version 3.5
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

#include "cache.h"
#include "cpuid.h"
#include "cpuinfo.h"
#include "hwctrl.h"
#include "io.h"
#include "keyboard.h"
#include "pmem.h"
#include "memsize.h"
#include "pci.h"
#include "screen.h"
#include "smp.h"
#include "temperature.h"
#include "vmem.h"

#include "badram.h"
#include "config.h"
#include "display.h"
#include "error.h"
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

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

// The following variables are written by the current "master" CPU, but may
// be read by all active CPUs.

static volatile int         init_state = 0;

static barrier_t            *start_barrier = NULL;
static spinlock_t           *start_mutex   = NULL;

static int8_t               pcpu_num_to_vcpu_num[MAX_PCPUS];

static volatile bool        start_run  = false;
static volatile bool        start_pass = false;
static volatile bool        start_test = false;
static volatile bool        rerun_test = false;

static volatile bool        dummy_run  = false;

static volatile int         window_num   = 0;
static volatile uintptr_t   window_start = 0;
static volatile uintptr_t   window_end   = 0;

static volatile int         test_stage = 0;

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

// These are exposed in test.h.

int                 num_vcpus = 1;

volatile int        master_vcpu = 0;

barrier_t           *run_barrier = NULL;

spinlock_t          *error_mutex = NULL;

volatile vm_map_t   vm_map[MAX_MEM_SEGMENTS];
volatile int        vm_map_size = 0;

volatile int        pass_num = 0;
volatile int        test_num = 0;

volatile bool       restart = false;
volatile bool       bail    = false;

volatile uintptr_t  test_addr[MAX_VCPUS];

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

#define BARRIER(enabled) \
    if (enabled) { \
        if (TRACE_BARRIERS) { \
            trace(my_pcpu, "Start barrier wait at %s line %i", __FILE__, __LINE__); \
        } \
        barrier_wait(start_barrier); \
    }

static void run_at(uintptr_t addr, int my_pcpu)
{
    uintptr_t *new_start_addr = (uintptr_t *)(addr + startup - _start);

    if (my_pcpu == 0) {
        memmove((void *)addr, &_start, _end - _start);
    }
    BARRIER(true);

    // We use a lock to ensure that only one CPU at a time jumps to
    // the new code. Some of the startup stuff is not thread safe!
    spin_lock(start_mutex);

    goto *new_start_addr;
}

static void global_init(void)
{
    floppy_off();

    cache_on();

    cpuid_init();

    screen_init();

    cpuinfo_init();

    pmem_init();

    pci_init();

    badram_init();

    config_init();

    keyboard_init(pause_at_start);

    display_init();

    error_init();

    initial_config();

    clear_message_area();

    display_available_cpus(num_pcpus);

    num_vcpus = 0;
    for (int i = 0; i < num_pcpus; i++) {
        if (pcpu_state[i] == CPU_STATE_ENABLED) {
            pcpu_num_to_vcpu_num[i] = num_vcpus;
            num_vcpus++;
        }
    }
    display_enabled_cpus(num_vcpus);

    master_vcpu = 0;

    if (enable_temperature) {
        int temp = get_cpu_temperature();
        if (temp > 0) {
            display_temperature(temp);
        } else {
            enable_temperature = false;
            no_temperature = true;
        }
    }
    if (enable_trace) {
        display_pinned_message(0, 0,"CPU Trace");
        display_pinned_message(1, 0,"--- ----------------------------------------------------------------------------");
        set_scroll_lock(true);
    }

    for (int i = 0; i < pm_map_size; i++) {
        trace(0, "pm %0*x - %0*x", 2*sizeof(uintptr_t), pm_map[i].start, 2*sizeof(uintptr_t), pm_map[i].end);
    }
    if (rsdp_addr != 0) {
        trace(0, "ACPI RSDP found in %s at %0*x", rsdp_source, 2*sizeof(uintptr_t), rsdp_addr);
    }

    start_barrier = smp_alloc_barrier(num_vcpus);
    run_barrier   = smp_alloc_barrier(num_vcpus);

    start_mutex   = smp_alloc_mutex();
    error_mutex   = smp_alloc_mutex();

    int failed = smp_start(pcpu_state);
    if (failed) {
        const char *message = "Failed to start CPU core %i. Press any key to reboot...";
        display_notice_with_args(strlen(message), message, failed);
        while (get_key() == 0) { }
        reboot();
    }

    start_run = true;
    dummy_run = true;
    restart = false;
}

static void setup_vm_map(uintptr_t win_start, uintptr_t win_end)
{
    vm_map_size = 0;

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
        uintptr_t seg_start = pm_map[i].start;
        uintptr_t seg_end   = pm_map[i].end;
        if (seg_start <= win_start) {
            seg_start = win_start;
        }
        if (seg_end >= win_end) {
            seg_end = win_end;
        }
        if (seg_start < seg_end && seg_start < win_end && seg_end > win_start) {
            vm_map[vm_map_size].pm_base_addr = seg_start;
            vm_map[vm_map_size].start        = first_word_mapping(seg_start);
            vm_map[vm_map_size].end          = last_word_mapping(seg_end - 1, sizeof(testword_t));
            vm_map_size++;
        }
    }
}

static void test_all_windows(int my_pcpu, int my_vcpu)
{
    int  active_cpus = 1;
    bool i_am_master = (my_vcpu == master_vcpu) || dummy_run;
    bool i_am_active = i_am_master;
    if (!dummy_run) {
        if (cpu_mode == PAR && test_list[test_num].cpu_mode == PAR) {
            active_cpus = num_vcpus;
            i_am_active = true;
        }
    }
    if (i_am_master) {
        barrier_init(run_barrier, active_cpus);
        if (!dummy_run) {
            if (active_cpus == 1) {
                display_active_cpu(my_pcpu);
            } else {
                display_all_active;
            }
        }
    }

    int iterations = test_list[test_num].iterations;
    if (pass_num == 0) {
        // Reduce iterations for a faster first pass.
        iterations /= 3;
    }

    // Loop through all possible windows.
    do {
        BARRIER(!dummy_run);
        if (bail) {
            break;
        }

        if (i_am_master) {
            if (window_num == 0 && test_list[test_num].stages > 1) {
                // A multi-stage test runs through all the windows at each stage.
                // Relocation may disrupt the test.
                window_num = 1;
            }
            if (window_num == 0 && pm_limit_lower >= HIGH_LOAD_ADDR) {
                // Avoid unnecessary relocation.
                window_num = 1;
            }
        }
        BARRIER(!dummy_run);

        // Relocate if necessary.
        if (window_num > 0) {
            if (!dummy_run && (uintptr_t)&_start != LOW_LOAD_ADDR) {
                run_at(LOW_LOAD_ADDR, my_pcpu);
            }
        } else {
            if (!dummy_run && (uintptr_t)&_start != HIGH_LOAD_ADDR) {
                run_at(HIGH_LOAD_ADDR, my_pcpu);
            }
        }

        if (i_am_master) {
            trace(my_vcpu, "start window %i", window_num);
            switch (window_num) {
              case 0:
                window_start = 0;
                window_end   = (HIGH_LOAD_ADDR >> PAGE_SHIFT);
                break;
              case 1:
                window_start = (HIGH_LOAD_ADDR >> PAGE_SHIFT);
                window_end   = VM_WINDOW_SIZE;
                break;
              default:
                window_start = window_end;
                window_end  += VM_WINDOW_SIZE;
            }
            setup_vm_map(window_start, window_end);
        }
        BARRIER(!dummy_run);

        if (!i_am_active) {
            continue;
        }

        if (vm_map_size == 0) {
            // No memory to test in this window.
            if (i_am_master) {
                window_num++;
            }
            continue;
        }

        if (dummy_run) {
            if (i_am_master) {
                ticks_per_test[pass_num][test_num] += run_test(-1, test_num, test_stage, iterations);
            }
        } else {
            if (!map_window(vm_map[0].pm_base_addr)) {
                // Either there is no PAE or we are at the PAE limit.
                break;
            }
            run_test(my_vcpu, test_num, test_stage, iterations);
        }

        if (i_am_master) {
            window_num++;
        }
    } while (window_end < pm_map[pm_map_size - 1].end);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

// The main entry point called from the startup code.

void main(void)
{
    int my_pcpu;
    if (init_state == 0) {
        // If this is the first time here, we must be CPU 0, as the APs haven't been started yet.
        my_pcpu = 0;
    } else {
        my_pcpu = smp_my_pcpu_num();
    }

    if (init_state < 2) {
        // Global initialisation is done by the boot CPU.
        if (my_pcpu == 0) {
            init_state = 1;
            global_init();
        } else {
            pcpu_state[my_pcpu] = CPU_STATE_RUNNING;
        }
    } else {
        // Release the lock taken in run_at().
        spin_unlock(start_mutex);
    }
    BARRIER(true);
    init_state = 2;

#if TEST_INTERRUPT
    if (my_pcpu == 0) {
        __asm__ __volatile__ ("int $1");
    }
#endif

    int my_vcpu = pcpu_num_to_vcpu_num[my_pcpu];

    // Due to the need to relocate ourselves in the middle of tests, the following
    // code cannot be written in the natural way as a set of nested loops. So we
    // have a single loop and use global state variables to allow us to restart
    // where we left off after each relocation.

    while (1) {
        BARRIER((my_vcpu != 0) || !dummy_run);
        if (my_vcpu == 0) {
            if (start_run) {
                pass_num = 0;
                start_pass = true;
                if (!dummy_run) {
                    display_start_run();
                    badram_init();
                    error_init();
                }
            }
            if (start_pass) {
                test_num = 0;
                start_test = true;
                if (dummy_run) {
                    ticks_per_pass[pass_num] = 0;
                } else {
                    display_start_pass();
                }
            }
            if (start_test) {
                trace(my_vcpu, "start test %i", test_num);
                test_stage = 0;
                rerun_test = true;
                if (dummy_run) {
                    ticks_per_test[pass_num][test_num] = 0;
                } else if (test_list[test_num].enabled) {
                    display_start_test();
                }
                bail = false;
            }
            if (rerun_test) {
                window_num   = 0;
                window_start = 0;
                window_end   = 0;
            }
            start_run  = false;
            start_pass = false;
            start_test = false;
            rerun_test = false;
        }
        BARRIER(!dummy_run);
        if (test_list[test_num].enabled) {
            test_all_windows(my_pcpu, my_vcpu);
        }
        BARRIER(!dummy_run);
        if (my_vcpu != 0) {
            continue;
        }

        check_input();
        if (restart) {
            // The configuration has been changed.
            master_vcpu = 0;
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
                    master_vcpu = (master_vcpu + 1) % num_vcpus;
                    if (master_vcpu != 0) {
                        rerun_test = true;
                        continue;
                    }
                }
                break;
              case ONE:
                master_vcpu = (master_vcpu + 1) % num_vcpus;
                break;
              case SEQ:
                master_vcpu = (master_vcpu + 1) % num_vcpus;
                if (master_vcpu != 0) {
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
        }

        start_test = true;
        test_num++;
        if (test_num < NUM_TEST_PATTERNS) {
            continue;
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
                display_notice("** Pass completed, no errors **");
            }
        }
    }
}
