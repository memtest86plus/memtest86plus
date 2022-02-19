// SPDX-License-Identifier: GPL-2.0
#ifndef TESTS_H
#define TESTS_H
/**
 * \file
 *
 * Provides support for identifying and running the memory tests.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>

#include "config.h"

#define NUM_TEST_PATTERNS   11

typedef struct {
    bool            enabled;
    cpu_mode_t      cpu_mode;
    int             stages;
    int             iterations;
    int             errors;
    char            *description;
} test_pattern_t;

extern test_pattern_t test_list[NUM_TEST_PATTERNS];

typedef enum { FAST_PASS, FULL_PASS, NUM_PASS_TYPES } pass_type_t;

extern int ticks_per_pass[NUM_PASS_TYPES];
extern int ticks_per_test[NUM_PASS_TYPES][NUM_TEST_PATTERNS];

int run_test(int my_cpu, int test, int stage, int iterations);

#endif // TESTS_H
