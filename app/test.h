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

/**
 * A barrier used when running tests. Only the scheduler/control code may use
 * it directly; test implementations use test_run_barrier().
 */
extern barrier_t *run_barrier;

/**
 * A mutex used when reporting errors or printing trace information.
 */
extern spinlock_t *error_mutex;

/**
 * The dense per-CPU chunk index within the current context team, rebuilt at
 * every wave binding.
 */
extern uint16_t test_team_chunk_index[MAX_CPUS];

/**
 * Cross-context cancellation request (0 or 1); atomic access only.
 */
extern uint32_t global_cancel_requested;

/**
 * Returns true when the calling context has been asked to stop (NUMA_PAR
 * cancellation published through the context barrier). False in legacy modes,
 * which use the plain `bail` flag.
 */
bool test_stop_requested(void);

/**
 * True when the calling CPU is the primary context master of the current
 * wave, the only context master allowed to update the test-specific display
 * fields without ui_mutex.
 */
bool test_is_primary_context_master(void);

/**
 * Publishes a cross-context cancellation request (release store) from a
 * context master after the UI requested a bail; every context converges on
 * the same exit path at its next safe point.
 */
void test_publish_cancel_request(void);

/**
 * Requests a stop of the given execution context; the first terminal reason
 * wins.
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
 * The list of memory segments currently mapped into virtual memory for each
 * execution context.
 */
extern vm_map_t vm_map[VMEM_MAX_CONTEXTS][MAX_MEM_SEGMENTS];
/**
 * The number of memory segments currently mapped into virtual memory for each
 * execution context.
 */
extern int vm_map_size[VMEM_MAX_CONTEXTS];

/**
 * The number of pages currently mapped into virtual memory for each execution
 * context.
 */
extern size_t num_mapped_pages[VMEM_MAX_CONTEXTS];

/**
 * The mutable per-execution-context state needed by the test scheduler and
 * test implementations. Context 0 is the legacy-mode context.
 */
typedef struct {
    uint32_t    proximity_domain_idx;
    int         master_cpu_num;
    unsigned int team_cpu_count;
    unsigned int active_cpu_count;
    int         window_index;
    uintptr_t   window_start;
    uintptr_t   window_end;
    bool        windows_exhausted;   // sticky per binding, published by the context barrier
    int         status;
} test_context_t;

/**
 * The per-execution-context test state.
 */
extern test_context_t test_contexts[VMEM_MAX_CONTEXTS];

/**
 * Returns the execution context of the calling CPU. In legacy modes and
 * during the BSP-only dummy run this is always context 0.
 */
test_context_t *test_context(void);

/**
 * Returns the index of the execution context of the calling CPU.
 */
int test_context_index(void);

/**
 * Returns the execution context currently bound to the given CPU, clamped
 * to 0 for unbound or invalid ordinals.
 */
int execution_context_for_cpu(int cpu);

/**
 * Returns the barrier to use for test-internal waits from the calling CPU:
 * the current context barrier in active NUMA_PAR, the legacy run_barrier
 * otherwise.
 */
barrier_t *test_run_barrier(void);

#define master_cpu       (test_context()->master_cpu_num)
#define num_active_cpus  (test_context()->active_cpu_count)
#define window_num       (test_context()->window_index)

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
