// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.

#include <stdbool.h>
#include <stdint.h>

#include "cpuid.h"
#include "cpuinfo.h"
#include "hwctrl.h"
#include "io.h"
#include "keyboard.h"
#include "serial.h"
#include "pmem.h"
#include "smbios.h"
#include "smbus.h"
#include "temperature.h"
#include "tsc.h"

#include "barrier.h"
#include "spinlock.h"

#include "config.h"
#include "error.h"
#include "githash.h"

#include "tests.h"

#include "display.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define SPINNER_PERIOD  100     // milliseconds

#define NUM_SPIN_STATES 4

static const char spin_state[NUM_SPIN_STATES] = { '|', '/', '-', '\\' };

static const char *cpu_mode_str[] = { "PAR", "SEQ", "RR " };

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static bool scroll_lock = false;
static bool scroll_wait = false;

static int spin_idx = 0;        // current spinner position

static int pass_ticks = 0;      // current value (ticks_per_pass is final value)
static int test_ticks = 0;      // current value (ticks_per_test is final value)

static int pass_bar_length = 0; // currently displayed length
static int test_bar_length = 0; // currently displayed length

static uint64_t run_start_time = 0; // TSC time stamp
static uint64_t next_spin_time = 0; // TSC time stamp

static int prev_sec = -1;               // previous second
static bool timed_update_done = false;  // update cycle status

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int scroll_message_row;

int max_cpu_temp = 0;

display_mode_t display_mode = DISPLAY_MODE_NA;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void display_init(void)
{
    cursor_off();

    clear_screen();

    set_foreground_colour(BLACK);
    set_background_colour(WHITE);
    clear_screen_region(0, 0, 0, 27);
    prints(0, 0, "     Memtest86+ v6.00b1");
    set_foreground_colour(RED);
    printc(0, 14, '+');
    set_foreground_colour(WHITE);
    set_background_colour(BLUE);
    prints(0,28,                             "| ");
    prints(1, 0, "CLK/Temp:   N/A             | Pass   % ");
    prints(2, 0, "L1 Cache:   N/A             | Test   % ");
    prints(3, 0, "L2 Cache:   N/A             | Test #   ");
    prints(4, 0, "L3 Cache:   N/A             | Testing: ");
    prints(5, 0, "Memory  :   N/A             | Pattern: ");
    prints(6, 0, "--------------------------------------------------------------------------------");
    prints(7, 0, "CPU:                      SMP: N/A        | Time:           Status: Init. ");
    prints(8, 0, "Using:                                    | Pass:           Errors: ");
    prints(9, 0, "--------------------------------------------------------------------------------");

    // Redraw lines using box drawing characters.
    // Disable if TTY is enabled to avoid VT100 char replacements
    if (!enable_tty) {
        for (int i = 0;i < 80; i++) {
            print_char(6, i, 0xc4);
            print_char(9, i, 0xc4);
        }
        for (int i = 0; i < 6; i++) {
            print_char(i, 28, 0xb3);
        }
        for (int i = 7; i < 10; i++) {
            print_char(i, 42, 0xb3);
        }
        print_char(6, 28, 0xc1);
        print_char(6, 42, 0xc2);
        print_char(9, 42, 0xc1);
    }

    set_foreground_colour(BLUE);
    set_background_colour(WHITE);
    clear_screen_region(ROW_FOOTER, 0, ROW_FOOTER, SCREEN_WIDTH - 1);
    prints(ROW_FOOTER, 0, " <ESC> Exit  <F1> Configuration  <Space> Scroll Lock");
    prints(ROW_FOOTER, 64, "6.00.");
    prints(ROW_FOOTER, 69, GIT_HASH);
#if TESTWORD_WIDTH > 32
    prints(ROW_FOOTER, 76, ".x64");
#else
    prints(ROW_FOOTER, 76, ".x32");
#endif
    set_foreground_colour(WHITE);
    set_background_colour(BLUE);

    if (cpu_model) {
        display_cpu_model(cpu_model);
    }
    if (clks_per_msec) {
        display_cpu_clk((int)(clks_per_msec / 1000));
    }
#if TESTWORD_WIDTH < 64
    if (cpuid_info.flags.lm) {
        display_cpu_addr_mode(" [LM]");
    } else if (cpuid_info.flags.pae) {
        display_cpu_addr_mode("[PAE]");
    }
#endif
    display_cpu_cache_mode();
    if (l1_cache) {
        display_l1_cache_size(l1_cache);
    }
    if (l2_cache) {
        display_l2_cache_size(l2_cache);
    }
    if (l3_cache) {
        display_l3_cache_size(l3_cache);
    }
    if (l1_cache_speed) {
        display_l1_cache_speed(l1_cache_speed);
    }
    if (l2_cache_speed) {
        display_l2_cache_speed(l2_cache_speed);
    }
    if (l3_cache_speed) {
        display_l3_cache_speed(l3_cache_speed);
    }
    if (ram_speed) {
        display_ram_speed(ram_speed);
    }
    if (num_pm_pages) {
        // Round to nearest MB.
        display_memory_size(1024 * ((num_pm_pages + 128) / 256));
    }

    scroll_message_row = ROW_SCROLL_T;
}

void display_cpu_topology(void)
{
    extern int num_enabled_cpus;
    int num_cpu_sockets = 1;

    if (smp_enabled) {
        display_threading(num_enabled_cpus, cpu_mode_str[cpu_mode]);
    } else {
        display_threading_disabled();
    }

    // If topology failed, assume topology according to APIC
    if (cpuid_info.topology.core_count <= 0) {

        cpuid_info.topology.core_count = num_enabled_cpus;
        cpuid_info.topology.thread_count = num_enabled_cpus;

        if(cpuid_info.flags.htt && num_enabled_cpus >= 2 && num_enabled_cpus % 2 == 0) {
            cpuid_info.topology.core_count /= 2;
        }
    }

    // Compute number of sockets according to individual CPU core count
    if (num_enabled_cpus > cpuid_info.topology.thread_count &&
       num_enabled_cpus % cpuid_info.topology.thread_count == 0) {

        num_cpu_sockets  = num_enabled_cpus / cpuid_info.topology.thread_count;
    }


    // Temporary workaround for Hybrid CPUs.
    // TODO: run cpuid on each core to get correct P+E topology
    if (cpuid_info.topology.is_hybrid) {
        display_cpu_topo_hybrid(cpuid_info.topology.thread_count);
        return;
    }

    // Condensed display for multi-socket motherboard
    if (num_cpu_sockets > 1) {
        display_cpu_topo_multi_socket(num_cpu_sockets,
                                      num_cpu_sockets * cpuid_info.topology.core_count,
                                      num_cpu_sockets * cpuid_info.topology.thread_count);
        return;
    }

    if (cpuid_info.topology.thread_count < 100) {
        display_cpu_topo(cpuid_info.topology.core_count,
                         cpuid_info.topology.thread_count);
    } else {
        display_cpu_topo_short(cpuid_info.topology.core_count,
                               cpuid_info.topology.thread_count);
    }

}

void post_display_init(void)
{
    print_smbios_startup_info();
    print_smbus_startup_info();

    if (false) {
        // Try to get RAM information from IMC (TODO)
        display_spec_mode("IMC: ");
        display_spec(ram.freq, ram.type, ram.tCL, ram.tRCD, ram.tRP, ram.tRAS);
        display_mode = DISPLAY_MODE_IMC;
    } else if (ram.freq > 0 && ram.tCL > 1) {
        // If not available, grab max memory specs from SPD
        display_spec_mode("RAM: ");
        display_spec(ram.freq, ram.type, ram.tCL, ram.tRCD, ram.tRP, ram.tRAS);
        display_mode = DISPLAY_MODE_SPD;
    } else {
        // If nothing available, fallback to "Using Core" Display
        display_mode = DISPLAY_MODE_NA;
    }
}

void display_start_run(void)
{
    if (!enable_trace && !enable_sm) {
        clear_message_area();
    }

    clear_screen_region(7, 49, 7, 57);                  // run time
    clear_screen_region(8, 49, 8, 57);                  // pass number
    clear_screen_region(8, 68, 8, SCREEN_WIDTH - 1);    // error count
    display_pass_count(0);
    display_error_count(0);
    if (clks_per_msec > 0) {
        // If we've measured the CPU speed, we know the TSC is available.
        run_start_time = get_tsc();
        next_spin_time = run_start_time + SPINNER_PERIOD * clks_per_msec;
    }
    display_spinner('-');
    display_status("Testing");

    if (enable_tty){
        tty_full_redraw();
    }
}

void display_start_pass(void)
{
    clear_screen_region(1, 39, 1, SCREEN_WIDTH - 1);    // progress bar
    display_pass_percentage(0);
    pass_bar_length = 0;
    pass_ticks = 0;
}

void display_start_test(void)
{
    clear_screen_region(2, 39, 3, SCREEN_WIDTH - 1);    // progress bar, test details
    clear_screen_region(4, 39, 4, SCREEN_WIDTH - 6);    // Avoid erasing paging mode
    clear_screen_region(5, 39, 5, SCREEN_WIDTH - 1);
    clear_screen_region(3, 36, 3, 37);                  // test number
    display_test_percentage(0);
    display_test_number(test_num);
    display_test_description(test_list[test_num].description);
    test_bar_length = 0;
    test_ticks = 0;
}

void check_input(void)
{
    switch (get_key()) {
      case ESC:
        clear_message_area();
        display_notice("Rebooting...");
        reboot();
        break;
      case '1':
        config_menu(false);
        break;
      case ' ':
        set_scroll_lock(!scroll_lock);
        break;
      case '\n':
        scroll_wait = false;
        break;
      default:
        break;
    }
}

void set_scroll_lock(bool enabled)
{
    scroll_lock = enabled;
    set_foreground_colour(BLUE);
    prints(ROW_FOOTER, 48, scroll_lock ? "unlock" : "lock  ");
    set_foreground_colour(WHITE);
}

void toggle_scroll_lock(void)
{
    set_scroll_lock(!scroll_lock);
}

void scroll(void)
{
    if (scroll_message_row < ROW_SCROLL_B) {
        scroll_message_row++;
    } else {
        if (scroll_lock) {
            display_footer_message("<Enter> Single step     ");
        }
        scroll_wait = true;
        do {
            check_input();
        } while (scroll_wait && scroll_lock);

        scroll_wait = false;
        clear_footer_message();
        scroll_screen_region(ROW_SCROLL_T, 0, ROW_SCROLL_B, SCREEN_WIDTH - 1);
    }
}

void do_tick(int my_cpu)
{
    int act_sec = 0;
    bool use_spin_wait = (power_save < POWER_SAVE_HIGH);
    if (use_spin_wait) {
        barrier_spin_wait(run_barrier);
    } else {
        barrier_halt_wait(run_barrier);
    }
    if (master_cpu == my_cpu) {
        check_input();
        error_update();
    }
    if (use_spin_wait) {
        barrier_spin_wait(run_barrier);
    } else {
        barrier_halt_wait(run_barrier);
    }

    // Only the master CPU does the update.
    if (master_cpu != my_cpu) {
        return;
    }

    test_ticks++;
    pass_ticks++;

    pass_type_t pass_type = (pass_num == 0) ? FAST_PASS : FULL_PASS;

    int pct = 0;
    if (ticks_per_test[pass_type][test_num] > 0) {
        pct = 100 * test_ticks / ticks_per_test[pass_type][test_num];
        if (pct > 100) {
            pct = 100;
        }
    }
    display_test_percentage(pct);
    display_test_bar((BAR_LENGTH * pct) / 100);

    pct = 0;
    if (ticks_per_pass[pass_type] > 0) {
        pct = 100 * pass_ticks / ticks_per_pass[pass_type];
        if (pct > 100) {
            pct = 100;
        }
    }
    display_pass_percentage(pct);
    display_pass_bar((BAR_LENGTH * pct) / 100);

    bool update_spinner = true;
    if (clks_per_msec > 0) {
        uint64_t current_time = get_tsc();

        int secs  = (current_time - run_start_time) / (1000 * clks_per_msec);
        int mins  = secs / 60; secs %= 60; act_sec = secs;
        int hours = mins / 60; mins %= 60;
        display_run_time(hours, mins, secs);

        if (current_time >= next_spin_time) {
            next_spin_time = current_time + SPINNER_PERIOD * clks_per_msec;
        } else {
            update_spinner = false;
        }
    }

    /* ---------------
     * Timed functions
     * --------------- */

    // update spinner every SPINNER_PERIOD ms
    if (update_spinner) {
        spin_idx = (spin_idx + 1) % NUM_SPIN_STATES;
        display_spinner(spin_state[spin_idx]);
    }

    // This only tick one time per second
    if (!timed_update_done) {

        // Update temperature one time per second
        if (enable_temperature) {
            int actual_cpu_temp = get_cpu_temperature();

            if(max_cpu_temp < actual_cpu_temp ) {
                max_cpu_temp = actual_cpu_temp;
            }

            if(actual_cpu_temp != 0) {
                display_temperature(actual_cpu_temp, max_cpu_temp);
            }
        }

        // Update TTY one time every TTY_UPDATE_PERIOD second(s)
        if (enable_tty) {

            if (act_sec % tty_update_period == 0) {
                tty_partial_redraw();
            }
        }
        timed_update_done = true;
    }

    if (act_sec != prev_sec) {
        prev_sec = act_sec;
        timed_update_done = false;
    }
}

void do_trace(int my_cpu, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    spin_lock(error_mutex);
    scroll();
    printi(scroll_message_row, 0, my_cpu, 2, false, false);
    vprintf(scroll_message_row, 4, fmt, args);
    spin_unlock(error_mutex);
    va_end(args);
}
