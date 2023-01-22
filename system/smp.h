// SPDX-License-Identifier: GPL-2.0
#ifndef _SMP_H_
#define _SMP_H_
/**
 * \file
 *
 * Provides support for multi-threaded operation.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

#include "barrier.h"
#include "spinlock.h"

/**
 * The maximum number of CPU cores that can be used.
 */
#define MAX_CPUS       (1 + MAX_APS)

/**
 * The maximum number of APIC IDs.
 */
#define MAX_APIC_IDS                256

/**
 * The maximum number of NUMA proximity domains.
 */
#define MAX_PROXIMITY_DOMAINS       MAX_APIC_IDS

/**
 * The current state of a CPU core.
 */
typedef enum  __attribute__ ((packed)) {
    CPU_STATE_DISABLED  = 0,
    CPU_STATE_ENABLED   = 1,
    CPU_STATE_RUNNING   = 2
} cpu_state_t;

/**
 * The number of available CPU cores. Initially this is 1, but may increase
 * after calling smp_init().
 */
extern int num_available_cpus;

/**
 * The number of distinct memory proximity domains. Initially this is 1, but
 * may increase after calling smp_init().
 */
extern int num_proximity_domains;

/**
 * Initialises the SMP state and detects the number of available CPU cores.
 */
void smp_init(bool smp_enable);

/**
 * Starts the APs listed as enabled in cpu_state. Returns 0 on success
 * or the index number of the lowest-numbered AP that failed to start.
 */
int smp_start(cpu_state_t cpu_state[MAX_CPUS]);

/**
 * Sends a non-maskable interrupt to the CPU core whose ordinal number
 * is cpu_num.
 */
void smp_send_nmi(int cpu_num);

/**
 * Returns the ordinal number of the calling CPU core.
 */
int smp_my_cpu_num(void);

/**
 * Return the index of the proximity domain corresponding to the current CPU number.
 * 1 in NUMA-unaware mode, >= 1 otherwise.
 */
uint32_t smp_get_proximity_domain_idx(int cpu_num);

/**
 * "Allocates" a CPU ID in the given proximity domain, for filling in NUMA-aware chunk index.
 * Returns the nth CPU ID found so far in the proximity domain.
 */
static inline uint8_t smp_alloc_cpu_in_proximity_domain(uint32_t proximity_domain_idx)
{
    extern uint8_t used_cpus_in_proximity_domain[MAX_PROXIMITY_DOMAINS];
    uint8_t chunk_index = used_cpus_in_proximity_domain[proximity_domain_idx];
    used_cpus_in_proximity_domain[proximity_domain_idx]++;
    return chunk_index;
}

/**
 * Computes the first span, limited to a single proximity domain, of the given memory range.
 */
int smp_narrow_to_proximity_domain(uintptr_t start, uintptr_t end, uint32_t * proximity_domain_idx, uintptr_t * new_start, uintptr_t * new_end);

//int count_cpus_for_proximity_domain_corresponding_to_range(uintptr_t start, uintptr_t end, uint32_t proximity_domain_idx);

//void get_memory_affinity_entry(int idx, uint32_t * proximity_domain_idx, uint64_t * start, uint64_t * end);

/**
 * Allocates and initialises a barrier object in pinned memory.
 */
barrier_t *smp_alloc_barrier(int num_threads);

/**
 * Allocates and initialises a spinlock object in pinned memory.
 */
spinlock_t *smp_alloc_mutex();

#endif /* _SMP_H_ */
