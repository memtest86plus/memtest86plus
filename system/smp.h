// SPDX-License-Identifier: GPL-2.0
#ifndef _SMP_H_
#define _SMP_H_
/*
 * Provides support for multi-threaded operation.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

#include "barrier.h"
#include "spinlock.h"

/*
 * The maximum number of CPU cores that can be used. Currently this is limited
 * to 256 both by the number of available APIC IDs and the need to fit both
 * the program and the CPU stacks in low memory.
 */
#define MAX_CPUS       256

/*
 * The current state of a CPU core.
 */
typedef enum  __attribute__ ((packed)) {
    CPU_STATE_DISABLED  = 0,
    CPU_STATE_ENABLED   = 1,
    CPU_STATE_RUNNING   = 2
} cpu_state_t;

/*
 * The number of available CPU cores. Initially this is 1, but may increase
 * after calling smp_init().
 */
extern int num_available_cpus;

/*
 * The search step that located the ACPI RSDP (for debug).
 */
extern const char *rsdp_source;
/*
 * The address of the ACPI RSDP (for debug).
 */
extern uintptr_t rsdp_addr;

/*
 * Initialises the SMP state and detects the number of available CPU cores.
 */
void smp_init(bool smp_enable);

/*
 * Starts the APs listed as enabled in cpu_state. Returns 0 on success
 * or the index number of the lowest-numbered AP that failed to start.
 */
int smp_start(cpu_state_t cpu_state[MAX_CPUS]);

/*
 * Returns the ordinal number of the calling CPU core.
 */
int smp_my_cpu_num(void);

/*
 * Allocates and initialises a barrier object in pinned memory.
 */
barrier_t *smp_alloc_barrier(int num_threads);

/*
 * Allocates and initialises a spinlock object in pinned memory.
 */
spinlock_t *smp_alloc_mutex();

#endif /* _SMP_H_ */
