#!/usr/bin/env python3
"""Exercise the current numa_window_step with two host threads.

The first test's final tick releases both CPUs, then deliberately holds its
peer inside run_test. The master must wait for that peer before publishing
the next map. No firmware or privileged memory accesses are involved.
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def extract_function(source, signature):
    # main.c also has a forward declaration; the last occurrence is its body.
    start = source.rindex(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREAMBLE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define VM_WINDOW_SIZE 256
enum { CONTEXT_OK, CONTEXT_CANCELLED, CONTEXT_MAP_FAILED };
enum { NORMAL, EMPTY, CANCEL_IN_TEST, TERMINAL, MAP_FAILURE, EXHAUSTED };
typedef struct {
    int master_cpu_num, window_index, status;
    uintptr_t window_start, window_end;
    bool windows_exhausted;
} test_context_t;
typedef struct { uintptr_t end; } pm_map_t;
typedef struct { uintptr_t pm_base_addr; } vm_map_t;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned arrived, generation;
} barrier_t;

static test_context_t test_contexts[1];
static pm_map_t pm_map[1];
static int pm_map_size = 1;
static vm_map_t vm_map[1][2];
static unsigned num_mapped_pages[1];
static int test_num, test_stage, scenario;
static barrier_t barrier;
static pthread_mutex_t events = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static _Thread_local int current_cpu;
static unsigned waits[2], test_calls[2], steps[2], setups;
static uint64_t test_pages_done;
static bool peer_held, release_peer, master_exit_waiting, premature_map;
static bool master_publication_held, release_publication, peer_decision_waiting;
static bool map_attempted;
static bool result[2];

static int load_test_status(int context)
{
    return __atomic_load_n(&test_contexts[context].status, __ATOMIC_ACQUIRE);
}

static void context_record_failure(int context, int status)
{
    int expected = CONTEXT_OK;
    __atomic_compare_exchange_n(&test_contexts[context].status, &expected,
                               status, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

static barrier_t *test_run_barrier(void) { return &barrier; }

static void barrier_spin_wait(barrier_t *b)
{
    unsigned call = ++waits[current_cpu];
    pthread_mutex_lock(&events);
    if (scenario == NORMAL && current_cpu == 0 && call == 6) {
        master_exit_waiting = true;
        pthread_cond_broadcast(&changed);
    }
    if (scenario == MAP_FAILURE && current_cpu == 1 && call == 2) {
        peer_decision_waiting = true;
        pthread_cond_broadcast(&changed);
    }
    pthread_mutex_unlock(&events);

    pthread_mutex_lock(&b->mutex);
    unsigned generation = b->generation;
    if (++b->arrived == 2) {
        b->arrived = 0;
        ++b->generation;
        pthread_cond_broadcast(&b->cond);
    } else {
        while (generation == b->generation) {
            pthread_cond_wait(&b->cond, &b->mutex);
        }
    }
    pthread_mutex_unlock(&b->mutex);
    if (scenario == MAP_FAILURE && current_cpu == 0 && call == 1) {
        pthread_mutex_lock(&events);
        master_publication_held = true;
        pthread_cond_broadcast(&changed);
        while (!release_publication) {
            pthread_cond_wait(&changed, &events);
        }
        pthread_mutex_unlock(&events);
    }
}

static void window_page_bounds(int index, uintptr_t *start, uintptr_t *end)
{
    *start = index * VM_WINDOW_SIZE;
    *end = *start + VM_WINDOW_SIZE;
}

static void setup_vm_map(int context, uintptr_t start, uintptr_t end)
{
    (void)end;
    pthread_mutex_lock(&events);
    if (++setups == 2 && scenario == NORMAL && !release_peer) {
        premature_map = true;
        pthread_cond_broadcast(&changed);
    }
    vm_map[context][0].pm_base_addr = start;
    num_mapped_pages[context] = scenario == EMPTY ? 0 : 1;
    pthread_mutex_unlock(&events);
}

static bool map_window(int context, uintptr_t start)
{
    (void)context;
    (void)start;
    if (scenario == MAP_FAILURE) {
        pthread_mutex_lock(&events);
        map_attempted = true;
        pthread_cond_broadcast(&changed);
        pthread_mutex_unlock(&events);
    }
    return !(scenario == MAP_FAILURE && current_cpu == 1);
}

static int run_test(int cpu, int test, int stage, int iterations)
{
    (void)test;
    (void)stage;
    (void)iterations;
    ++test_calls[cpu];
    // Match run_test's initial barrier before a CPU can publish a tick stop.
    barrier_spin_wait(test_run_barrier());
    if (scenario == CANCEL_IN_TEST && cpu == 0) {
        context_record_failure(0, CONTEXT_CANCELLED);
    }
    // The final test tick is a rendezvous. It does not finish run_test.
    barrier_spin_wait(test_run_barrier());
    if (scenario == NORMAL && cpu == 1 && test_calls[cpu] == 1) {
        pthread_mutex_lock(&events);
        peer_held = true;
        pthread_cond_broadcast(&changed);
        while (!release_peer) {
            pthread_cond_wait(&changed, &events);
        }
        pthread_mutex_unlock(&events);
    }
    return 1;
}
"""

MAIN = r"""
static void *worker(void *argument)
{
    current_cpu = (int)(intptr_t)argument;
    unsigned limit = (scenario == NORMAL || scenario == EMPTY
                      || scenario == CANCEL_IN_TEST) ? 2 : 1;
    for (unsigned i = 0; i < limit; ++i) {
        ++steps[current_cpu];
        result[current_cpu] = numa_window_step(current_cpu, 0, 1);
        if (!result[current_cpu]) break;
    }
    return NULL;
}

static void exercise(int kind, unsigned expected_waits, unsigned expected_tests,
                     bool expected_result)
{
    scenario = kind;
    test_contexts[0] = (test_context_t){ .master_cpu_num = 0, .window_index = 1 };
    pm_map[0].end = 4 * VM_WINDOW_SIZE;
    if (kind == TERMINAL) test_contexts[0].status = CONTEXT_CANCELLED;
    if (kind == EXHAUSTED) {
        test_contexts[0].window_index = 2;
        test_contexts[0].window_end = pm_map[0].end;
    }
    setups = 0;
    peer_held = release_peer = master_exit_waiting = premature_map = false;
    master_publication_held = release_publication = peer_decision_waiting = false;
    map_attempted = false;
    for (int cpu = 0; cpu < 2; ++cpu) {
        waits[cpu] = test_calls[cpu] = steps[cpu] = 0;
        result[cpu] = false;
    }
    barrier = (barrier_t){ .mutex = PTHREAD_MUTEX_INITIALIZER,
                          .cond = PTHREAD_COND_INITIALIZER };
    pthread_t threads[2];
    for (int cpu = 0; cpu < 2; ++cpu) {
        assert(pthread_create(&threads[cpu], NULL, worker, (void *)(intptr_t)cpu) == 0);
    }
    if (kind == NORMAL) {
        pthread_mutex_lock(&events);
        while (!premature_map && !(peer_held && master_exit_waiting)) {
            pthread_cond_wait(&changed, &events);
        }
        assert(!premature_map);
        assert(setups == 1);
        release_peer = true;
        pthread_cond_broadcast(&changed);
        pthread_mutex_unlock(&events);
    }
    if (kind == MAP_FAILURE) {
        pthread_mutex_lock(&events);
        while (!(master_publication_held && (peer_decision_waiting || map_attempted))) {
            pthread_cond_wait(&changed, &events);
        }
        assert(!map_attempted);
        release_publication = true;
        pthread_cond_broadcast(&changed);
        pthread_mutex_unlock(&events);
    }
    for (int cpu = 0; cpu < 2; ++cpu) {
        assert(pthread_join(threads[cpu], NULL) == 0);
        assert(waits[cpu] == expected_waits);
        assert(test_calls[cpu] == expected_tests);
        assert(result[cpu] == expected_result);
    }
    assert(pthread_mutex_destroy(&barrier.mutex) == 0);
    assert(pthread_cond_destroy(&barrier.cond) == 0);
}

int main(void)
{
    exercise(NORMAL, 12, 2, true);
    exercise(EMPTY, 6, 0, true);
    exercise(CANCEL_IN_TEST, 8, 1, false);
    exercise(TERMINAL, 2, 0, false);
    exercise(MAP_FAILURE, 3, 0, false);
    exercise(EXHAUSTED, 2, 0, false);
    puts("numa_window_step: delayed peer, empty windows, cancellation, map failure,"
         " terminal status and exhaustion passed");
    return 0;
}
"""


def main():
    function = extract_function(
        (ROOT / "app/numa_par.c").read_text(), "static bool numa_window_step("
    )
    with tempfile.TemporaryDirectory(prefix="numa-window-barrier-") as directory:
        source = Path(directory) / "regression.c"
        binary = Path(directory) / "regression"
        source.write_text(PREAMBLE + "\n" + function + "\n" + MAIN)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(
            compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                        str(source), "-o", str(binary)],
            check=True,
        )
        # A missing or unbalanced rendezvous must fail instead of hanging CI.
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
