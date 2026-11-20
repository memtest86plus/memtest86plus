// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Martin Whitaker.

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "smp.h"

#include "cpulocal.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// An arbitrary value that is unlikely to appear in a stack frame.
#define STACK_CANARY    UINT32_C(0x446D6153)

#define UI_STACK_SIZE   8192

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int local_bytes_used = 0;

// Whether each stack canary is currently valid. The stack area is not
// preserved across program relocations, hence the arm/disarm cycle.
static bool canary_armed[MAX_CPUS];

// Stack shared by the APs for the keyboard and menu code, too deep for an AP stack slot.
static uint8_t  ui_stack[UI_STACK_SIZE] __attribute__((aligned(16)));
static bool     ui_stack_busy = false;
static int      ui_stack_overflow_cpu = -1;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static uint32_t *stack_canary_addr(int cpu_num)
{
    uintptr_t slot_bottom = (uintptr_t)_stacks;
    if (cpu_num > 0) {
        slot_bottom += BSP_STACK_SIZE + (uintptr_t)(cpu_num - 1) * AP_STACK_SIZE;
    }
    return (uint32_t *)slot_bottom;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int allocate_local_flag(void)
{
    if (local_bytes_used == LOCALS_SIZE) {
        return -1;
    }
    return local_bytes_used += sizeof(bool);
}

void stack_canary_arm(int cpu_num)
{
    if (cpu_num < 0 || cpu_num >= MAX_CPUS) {
        return;
    }
    *stack_canary_addr(cpu_num) = STACK_CANARY;
    canary_armed[cpu_num] = true;
}

void stack_canary_disarm_all(void)
{
    for (int i = 0; i < MAX_CPUS; i++) {
        canary_armed[i] = false;
    }
}

int stack_canary_check(void)
{
    for (int i = 0; i < num_available_cpus; i++) {
        if (canary_armed[i] && *stack_canary_addr(i) != STACK_CANARY) {
            return i;
        }
    }
    return ui_stack_overflow_cpu;
}

void call_on_ui_stack(void (*fn)(void))
{
    int my_cpu = smp_my_cpu_num();

    // The BSP stack is large enough. A busy UI stack means a nested call: stay on the current stack.
    if (my_cpu == 0 || __atomic_test_and_set(&ui_stack_busy, __ATOMIC_ACQUIRE)) {
        fn();
        return;
    }
    uint32_t *canary = (uint32_t *)ui_stack;
    *canary = STACK_CANARY;
    call_on_stack(fn, ui_stack + UI_STACK_SIZE);
    if (*canary != STACK_CANARY) {
        ui_stack_overflow_cpu = my_cpu;
    }
    __atomic_clear(&ui_stack_busy, __ATOMIC_RELEASE);
}
