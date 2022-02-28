// SPDX-License-Identifier: GPL-2.0
#ifndef BARRIER_H
#define BARRIER_H
/**
 * \file
 *
 * Provides a barrier synchronisation primitive.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include "cpulocal.h"

#include "spinlock.h"

/**
 * A barrier object.
 */
typedef struct
{
    int     flag_num;
    int     num_threads;
    int     count;
} barrier_t;

/**
 * Initialises a new barrier to block the specified number of threads.
 */
void barrier_init(barrier_t *barrier, int num_threads);

/**
 * Resets an existing barrier to block the specified number of threads.
 */
void barrier_reset(barrier_t *barrier, int num_threads);

/**
 * Waits for all threads to arrive at the barrier. A CPU core spins in an
 * idle loop when waiting.
 */
void barrier_spin_wait(barrier_t *barrier);

/**
 * Waits for all threads to arrive at the barrier. A CPU core halts when
 * waiting.
 */
void barrier_halt_wait(barrier_t *barrier);

#endif // BARRIER_H
