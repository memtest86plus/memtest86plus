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
 * The maximum number of active physical CPUs. There must be room in
 * low memory for the program and all the CPU stacks.
 */
#define MAX_PCPUS   256

/*
 * An error code returned by smp_start().
 */
typedef enum {
    SMP_ERR_NONE                    = 0,
    SMP_ERR_BOOT_TIMEOUT            = 1,
    SMP_ERR_STARTUP_IPI_NOT_SENT    = 2,
    SMP_ERR_STARTUP_IPI_ERROR       = 0x100 // error code will be added to this
} smp_error_t;

/*
 * The number of available physical CPUs. Initially this is 1, but may
 * increase after calling smp_init().
 */
extern int num_pcpus;

/*
 * The search step that located the ACPI RSDP (for debug).
 */
extern const char *rsdp_source;
/*
 * The address of the ACPI RSDP (for debug).
 */
extern uintptr_t rsdp_addr;

/*
 * Initialises the SMP state and detects the number of physical CPUs.
 */
void smp_init(bool smp_enable);

/*
 * Starts the selected APs.
 */
smp_error_t smp_start(bool enable_pcpu[MAX_PCPUS]);

/*
 * Signals that an AP has booted.
 */
void smp_set_ap_booted(int pcpu_num);

/*
 * Returns the ordinal number of the calling PCPU.
 */
int smp_my_pcpu_num(void);

/*
 * Allocates and initialises a barrier object in pinned memory.
 */
barrier_t *smp_alloc_barrier(int num_threads);

/*
 * Allocates and initialises a spinlock object in pinned memory.
 */
spinlock_t *smp_alloc_mutex();

#endif /* _SMP_H_ */
