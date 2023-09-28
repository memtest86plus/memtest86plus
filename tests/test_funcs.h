// SPDX-License-Identifier: GPL-2.0
#ifndef TEST_FUNCS_H
#define TEST_FUNCS_H
/**
 * \file
 *
 * Provides the prototypes for the basic test functions used to implement
 * the tests.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>

#include "test.h"

int test_addr_walk1(int my_cpu);

int test_own_addr1(int my_cpu);

int test_own_addr2(int my_cpu, int stage);

int test_mov_inv_fixed(int my_cpu, int iterations, testword_t pattern1, testword_t pattern2, int simd);

int test_mov_inv_walk1(int my_cpu, int iterations, int offset, bool inverse);

int test_mov_inv_random(int my_cpu);

int test_modulo_n(int my_cpu, int iterations, testword_t pattern1, testword_t pattern2, int n, int offset);

int test_block_move(int my_cpu, int iterations);

int test_bit_fade(int my_cpu, int stage, int sleep_secs);

#endif // TEST_FUNCS_H
