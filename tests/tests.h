// SPDX-License-Identifier: GPL-2.0
#ifndef TESTS_H
#define TESTS_H
/**
 * \file
 *
 * Provides support for identifying and running the memory tests.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>

#include "config.h"

#define NUM_TEST_PATTERNS   12

// The largest number of stages any test pattern uses (bit-fade: 12).
#define MAX_TEST_STAGES     12

// The test-work class used by the NUMA_PAR expected-work accounting. The
// dummy calibration records per stage the single-master tick count over the
// whole map; the NUMA_PAR aggregate differs per test class:
//  - window-fixed tests (address walking, rowhammer): every context master
//    ticks a fixed number of times per nonempty window it owns, so the
//    total follows the number of nonempty context/window pairs;
//  - segment-fixed tests (all other memory-touching stages): ticks follow
//    the per-context segment counts. Every scheduler window is at most one
//    tick block (a static assertion in main.c ties the per-segment model to
//    the 1 GiB window/block sizes), so each window-clipped segment ticks
//    exactly once per active CPU and per-segment work equals per-block
//    work.
enum {
    TEST_WORK_WINDOW_FIXED,
    TEST_WORK_SEGMENT_FIXED
};

typedef struct {
    bool            enabled;
    uint8_t         cpu_mode;
    uint8_t         work_class;   // TEST_WORK_* (fits in the struct padding)
    int             stages;
    int             iterations;
    int             errors;
    char            description[40];
} test_pattern_t;

extern test_pattern_t test_list[NUM_TEST_PATTERNS];

typedef enum { FAST_PASS, FULL_PASS, NUM_PASS_TYPES } pass_type_t;

extern int ticks_per_pass[NUM_PASS_TYPES];
extern int ticks_per_test[NUM_PASS_TYPES][NUM_TEST_PATTERNS];

/**
 * The per-stage portion of the dummy-run calibration: accumulated per window
 * and per stage, so each stage's expected work can use its own calibration
 * instead of the whole-test total.
 */
extern int ticks_per_stage[NUM_PASS_TYPES][NUM_TEST_PATTERNS][MAX_TEST_STAGES];

int run_test(int my_cpu, int test, int stage, int iterations);

/**
 * Patches the test descriptions that depend on the platform capabilities.
 * Must be called after simd_init().
 */
void test_list_init(void);

#endif // TESTS_H
