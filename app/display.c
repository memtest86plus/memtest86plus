// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.

#include <stdbool.h>
#include <stdint.h>

#include "cpuid.h"
#include "cpuinfo.h"
#include "hwctrl.h"
#include "io.h"
#include "keyboard.h"
#include "pmem.h"
#include "temperature.h"
#include "tsc.h"

#include "barrier.h"
#include "spinlock.h"

#include "config.h"
#include "error.h"

#include "tests.h"

#include "display.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define NUM_SPIN_STATES 4

static const char spin_state[NUM_SPIN_STATES] = { '|', '/', '-', '\\' };

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static bool scroll_lock = false;
static bool scroll_wait = false;

static int spin_idx[MAX_VCPUS]; // current spinner position

static int pass_ticks = 0;      // current value (ticks_per_pass is final value)
static int test_ticks = 0;      // current value (ticks_per_test is final value)

static int pass_bar_length = 0; // currently displayed length
static int test_bar_length = 0; // currently displayed length

static uint64_t run_start_time = 0; // TSC time stamp

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int scroll_message_row;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void display_init(void)
{
    cursor_off();

    clear_screen();

    set_foreground_colour(RED);
    set_background_colour(WHITE);
    clear_screen_region(0, 0, 0, 27);
#if TESTWORD_WIDTH > 32
    prints( 0, 0, "    PCMemTest-64  v1.3     ");
#else
    prints( 0, 0, "    PCMemTest-32  v1.3     ");
#endif
    set_foreground_colour(WHITE);
    set_background_colour(BLUE);
    prints( 0,28,                             "| ");
    prints( 1, 0, "CPU     : N/A               | Pass   % ");
    prints( 2, 0, "L1 Cache: N/A               | Test   % ");
    prints( 3, 0, "L2 Cache: N/A               | Test #   ");
    prints( 4, 0, "L3 Cache: N/A               | Testing: ");
    prints( 5, 0, "Memory  : N/A               | Pattern: ");
    prints( 6, 0, "-------------------------------------------------------------------------------");
    prints( 7, 0, "vCPU#:                                 | Time:            Temperature: N/A ");
    prints( 8, 0, "State:                                 | ");
    prints( 9, 0, "Cores:    Active /    Total (Run: All) | Pass:            Errors: ");
    prints(10, 0, "-------------------------------------------------------------------------------");

    // Redraw lines using box drawing characters.
    for (int i = 0; i < 80; i++) {
        print_char( 6, i, 0xc4);
        print_char(10, i, 0xc4);
    }
    for (int i = 0; i < 6; i++) {
        print_char(i, 28, 0xb3);
    }
    for (int i = 7; i < 10; i++) {
        print_char(i, 39, 0xb3);
    }
    print_char( 6, 28, 0xc1);
    print_char( 6, 39, 0xc2);
    print_char(10, 39, 0xc1);

    set_foreground_colour(BLUE);
    set_background_colour(WHITE);
    clear_screen_region(ROW_FOOTER, 0, ROW_FOOTER, SCREEN_WIDTH - 1);
    prints(ROW_FOOTER, 0, " <ESC> exit  <F1> configuration  <Space> scroll lock");
    set_foreground_colour(WHITE);
    set_background_colour(BLUE);

    if (cpu_model) {
        display_cpu_model(cpu_model);
    }
    if (clks_per_msec) {
        display_cpu_clk((int)(clks_per_msec / 1000));
    }
    if (cpuid_info.flags.lm) {
        display_cpu_addr_mode("(x64)");
    } else if (cpuid_info.flags.pae) {
        display_cpu_addr_mode("(PAE)");
    }
    if (l1_cache) {
        display_l1_cache_size(l1_cache);
    }
    if (l2_cache) {
        display_l2_cache_size(l2_cache);
    }
    if (l3_cache) {
        display_l3_cache_size(l3_cache);
    }
    if (num_pm_pages) {
        // Round to nearest MB.
        display_memory_size(1024 * ((num_pm_pages + 128) / 256));
    }

    scroll_message_row = ROW_SCROLL_T;
}

void display_start_run(void)
{
    if (!enable_trace) {
        clear_message_area();
    }
    clear_screen_region(7, 47, 9, 57);                  // run time
    clear_screen_region(9, 47, 9, 57);                  // pass number
    clear_screen_region(9, 66, 9, SCREEN_WIDTH - 1);    // error count
    display_pass_count(0);
    display_error_count(0);
    run_start_time = get_tsc();
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
    clear_screen_region(2, 39, 5, SCREEN_WIDTH - 1);    // progress bar, test details
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
            display_footer_message("<Enter> Single step");
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

void do_tick(int my_vcpu)
{
    spin_idx[my_vcpu] = (spin_idx[my_vcpu] + 1) % NUM_SPIN_STATES;
    display_spinner(my_vcpu, spin_state[spin_idx[my_vcpu]]);

    barrier_wait(run_barrier);
    if (master_vcpu == my_vcpu) {
        check_input();
        error_update();
    }
    barrier_wait(run_barrier);

    // Only the master CPU does the update.
    if (master_vcpu != my_vcpu) {
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

    if (cpuid_info.flags.rdtsc) {
        int secs  = (get_tsc() - run_start_time) / (1000 * clks_per_msec);
        int mins  = secs / 60; secs %= 60;
        int hours = mins / 60; mins %= 60;
        display_run_time(hours, mins, secs);
    }

    if (enable_temperature) {
        display_temperature(get_cpu_temperature());
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
