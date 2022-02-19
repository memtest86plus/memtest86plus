// SPDX-License-Identifier: GPL-2.0
#ifndef BARRIER_H
#define BARRIER_H
/**
 * \file
 *
 * Provides a barrier synchronisation primitive.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include "spinlock.h"

/**
 * A barrier object.
 */
typedef struct
{
    int             num_threads;
    volatile int    count;
    spinlock_t      lock;
    spinlock_t      st1;
    spinlock_t      st2;
} barrier_t;

/**
 * Initialises the barrier to block the specified number of threads.
 */
void barrier_init(barrier_t *barrier, int num_threads);

/**
 * Waits for all threads to arrive at the barrier.
 */
void barrier_wait(barrier_t *barrier);

#endif // BARRIER_H
