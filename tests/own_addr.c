// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from an extract of memtest86+ test.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// ----------------------------------------------------
// test.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "vmem.h"

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static int pattern_fill(int my_cpu, testword_t offset)
{
    int ticks = 0;

    if (my_cpu == master_cpu) {
        display_test_pattern_name("own address");
    }

    // Write each address with it's own address.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start = vm_map[i].start;
        testword_t *end   = vm_map[i].end;

        testword_t *p  = start;
        testword_t *pe = start;

        bool at_end = false;
        do {
            // take care to avoid pointer overflow
            if ((end - pe) >= SPIN_SIZE) {
                pe += SPIN_SIZE - 1;
            } else {
                at_end = true;
                pe = end;
            }
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)p;
            do {
                write_word(p, (testword_t)p + offset);
            } while (p++ < pe); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    flush_caches(my_cpu);

    return ticks;
}

static int pattern_check(int my_cpu, testword_t offset)
{
    int ticks = 0;

    // Check each address has its own address.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start = vm_map[i].start;
        testword_t *end   = vm_map[i].end;

        testword_t *p  = start;
        testword_t *pe = start;

        bool at_end = false;
        do {
            // take care to avoid pointer overflow
            if ((end - pe) >= SPIN_SIZE) {
                pe += SPIN_SIZE - 1;
            } else {
                at_end = true;
                pe = end;
            }
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)p;
            do {
                testword_t expect = (testword_t)p + offset;
                testword_t actual = read_word(p);
                if (unlikely(actual != expect)) {
                    data_error(p, expect, actual, true);
                }
            } while (p++ < pe); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    return ticks;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_own_addr1(int my_cpu)
{
    int ticks = 0;

    ticks += pattern_fill(my_cpu, 0);
    ticks += pattern_check(my_cpu, 0);

    return ticks;
}

int test_own_addr2(int my_cpu, int stage)
{
    int ticks = 0;

    testword_t offset;

    // Calculate the offset (in pages) between the virtual address and the physical address.
    offset = (vm_map[0].pm_base_addr / VM_WINDOW_SIZE) * VM_WINDOW_SIZE;
    offset = (offset >= VM_PINNED_SIZE) ? offset - VM_PINNED_SIZE : 0;
#if (ARCH_BITS == 64)
    // Convert to a byte address offset. This will translate the virtual address into a physical address.
    offset *= PAGE_SIZE;
#else
    // Convert to a VM window offset. This will get added into the LSBs of the virtual address.
    offset /= VM_WINDOW_SIZE;
#endif

    switch (stage) {
      case 0:
        ticks = pattern_fill(my_cpu, offset);
        break;
      case 1:
        ticks = pattern_check(my_cpu, offset);
        break;
      default:
        break;
    }

    return ticks;
}
