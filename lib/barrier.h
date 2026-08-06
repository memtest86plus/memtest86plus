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
 *
 * This is a quiescent-only operation with a single owner: no CPU may be
 * inside the barrier, may still enter its old generation, or may enter its
 * new generation until the owner publishes the new participant count. It is
 * intentionally not safe against concurrent waiters; callers provide the
 * surrounding publication protocol. The name documents that a new count is
 * published for the next generation, not that a running generation is torn
 * down.
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
