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

//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------

int local_bytes_used = 0;

// Whether each stack canary is currently valid. The stack area is not
// preserved across program relocations, hence the arm/disarm cycle.
static bool canary_armed[MAX_CPUS];

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
    return -1;
}
