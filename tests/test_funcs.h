// SPDX-License-Identifier: GPL-2.0
#ifndef TEST_FUNCS_H
#define TEST_FUNCS_H
/*
 * Provides the prototypes for the basic test functions used to implement
 * the tests.
 *
 * Copyright (C) 2020 Martin Whitaker.
 */

#include "test.h"

int test_addr_walk1(int my_vcpu);

int test_own_addr1(int my_vcpu);

int test_own_addr2(int my_vcpu, int stage);

int test_mov_inv_fixed(int my_vcpu, int iterations, testword_t pattern1, testword_t pattern2);

int test_mov_inv_walk1(int my_vcpu, int iterations, int offset, bool inverse);

int test_mov_inv_random(int my_vcpu);

int test_modulo_n(int my_vcpu, int iterations, testword_t pattern1, testword_t pattern2, int n, int offset);

int test_block_move(int my_vcpu, int iterations);

int test_bit_fade(int my_vcpu, int stage, int sleep_secs);

#endif // TEST_FUNCS_H
