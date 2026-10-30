// SPDX-License-Identifier: GPL-2.0
#ifndef TEST_H
#define TEST_H
/**
 * \file
 *
 * Provides types and variables used when performing the memory tests.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include "pmem.h"
#include "smp.h"

#include "barrier.h"
#include "spinlock.h"

/**
 * A mapping from a CPU core number to the index number of the memory chunk
 * it operates on when performing a memory test in parallel across all the
 * enabled cores (in the current proximity domain, when NUMA awareness is
 * enabled).
 */
extern uint16_t chunk_index[MAX_CPUS];
/**
 * An array where the count of used CPUs in the current proximity domain.
 */
extern uint16_t used_cpus_in_proximity_domain[MAX_PROXIMITY_DOMAINS];

/*
 * The number of CPU cores being used for the current test. This is always
 * either 1 or the full number of enabled CPU cores.
 */
extern int num_active_cpus;

/**
 * The current master CPU core.
 */
extern int master_cpu;

/* Legacy window number (context 0); NUMA_PAR uses test_context() fields. */
extern int window_num;

/**
 * A barrier used when running tests. The tests use test_run_barrier().
 */
extern barrier_t *run_barrier;

/**
 * A mutex used when reporting errors or printing trace information.
 */
extern spinlock_t *error_mutex;

/**
 * A mutex serializing input service and UI rendering by the context masters.
 */
extern spinlock_t *ui_mutex;

/**
 * True while a NUMA_PAR run is active; false in legacy modes and the dummy run.
 */
extern bool numa_run_active;

/**
 * The per-CPU chunk index within its context team, rebuilt at every wave binding.
 */
extern uint16_t test_team_chunk_index[MAX_CPUS];

/**
 * Aggregate pages tested in the current NUMA_PAR stage, by all contexts.
 */
extern uint64_t test_pages_done;

/**
 * Cross-context cancellation request (0 or 1); atomic access only.
 */
extern uint32_t global_cancel_requested;

/**
 * Returns true when the calling context has been asked to stop (`bail` in legacy modes).
 */
bool test_stop_requested(void);

/**
 * Publishes a cross-context cancellation request after the UI requested a bail.
 */
void test_publish_cancel_request(void);

/**
 * Requests a stop of the given execution context; the first terminal reason wins.
 */
void test_request_cancel(int context);

#if (ARCH_BITS == 64)
/**
 * The word width (in bits) used for memory testing.
 */
#define TESTWORD_WIDTH       64
/**
 * The number of hex digits needed to display a memory test word.
 */
#define TESTWORD_DIGITS      16
/**
 * The string representation of TESTWORDS_DIGITS
 */
#define TESTWORD_DIGITS_STR "16"
#else
/**
 * The word width (in bits) used for memory testing.
 */
#define TESTWORD_WIDTH      32
/**
 * The number of hex digits needed to display a memory test word.
 */
#define TESTWORD_DIGITS      8
/**
 * The string representation of TESTWORDS_DIGITS
 */
#define TESTWORD_DIGITS_STR "8"
#endif

/**
 * The word type used for memory testing.
 */
typedef uintptr_t testword_t;

/**
 * A virtual memory segment descriptor.
 */
typedef struct {
    uintptr_t   pm_base_addr;
    testword_t  *start;
    testword_t  *end;
    uint32_t    proximity_domain_idx;
} vm_map_t;

/**
 * The list of memory segments currently mapped into virtual memory, per context.
 */
extern vm_map_t vm_map[VMEM_MAX_CONTEXTS][MAX_MEM_SEGMENTS];
/**
 * The number of memory segments currently mapped into virtual memory, per context.
 */
extern int vm_map_size[VMEM_MAX_CONTEXTS];

/**
 * The number of pages currently mapped into virtual memory, per context.
 */
extern size_t num_mapped_pages[VMEM_MAX_CONTEXTS];

/**
 * The state of an execution context. Context 0 is the legacy-mode context.
 */
typedef struct {
    uint32_t    proximity_domain_idx;
    int         master_cpu_num;
    unsigned int team_cpu_count;
    unsigned int active_cpu_count;
    int         window_index;
    uintptr_t   window_start;
    uintptr_t   window_end;
    bool        windows_exhausted;   // the context is done (sticky per binding)
    int         status;
} test_context_t;

/**
 * The number of execution contexts bound in the current wave; 1 in legacy modes and the dummy run.
 */
extern unsigned int num_bound_execution_contexts;

/**
 * The per-execution-context test state.
 */
extern test_context_t test_contexts[VMEM_MAX_CONTEXTS];

/**
 * Returns the calling CPU's execution context (context 0 in legacy modes and the dummy run).
 */
test_context_t *test_context(void);

/**
 * Returns the index of the execution context of the calling CPU.
 */
int test_context_index(void);

/**
 * Returns the execution context bound to the given CPU, or 0 when unbound.
 */
int execution_context_for_cpu(int cpu);

/**
 * Returns the calling CPU's test barrier: its context barrier in NUMA_PAR, else run_barrier.
 */
barrier_t *test_run_barrier(void);

/**
 * True when the CPU is the master of context 0, the only one updating the test display fields
 * without ui_mutex. Never true for the dummy run (negative CPU).
 */
static inline bool test_is_primary_context_master(int my_cpu)
{
    if (my_cpu < 0) {
        return false;
    }
    if (VMEM_MAX_CONTEXTS <= 1 || !numa_run_active) {
        return my_cpu == master_cpu;
    }
    return my_cpu == test_contexts[0].master_cpu_num;
}

/**
 * The number of completed test passes.
 */
extern int pass_num;
/**
 * The current test number.
 */
extern int test_num;

/**
 * A flag indicating that testing should be restarted due to a configuration
 * change.
 */
extern bool restart;
/**
 * A flag indicating that the current test should be aborted.
 */
extern bool bail;

/**
 * The base address of the block of memory currently being tested.
 */
extern uintptr_t test_addr[MAX_CPUS];

#endif // TEST_H
