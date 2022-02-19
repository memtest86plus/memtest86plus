// SPDX-License-Identifier: GPL-2.0
#ifndef SPINLOCK_H
#define SPINLOCK_H
/**
 * \file
 *
 * Provides a lightweight mutex synchronisation primitive.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>

/**
 * A mutex object. Use spin_unlock() to initialise prior to first use.
 */
typedef volatile bool spinlock_t;

/**
 * Spins until the mutex is unlocked.
 */
static inline void spin_wait(spinlock_t *lock)
{
    if (lock) {
        while (*lock) {
            __builtin_ia32_pause();
        }
    }
}

/**
 * Spins until the mutex is unlocked, then locks the mutex.
 */
static inline void spin_lock(spinlock_t *lock)
{
    if (lock) {
        while (!__sync_bool_compare_and_swap(lock, false, true)) {
            do {
                __builtin_ia32_pause();
            } while (*lock);
        }
        __sync_synchronize();
    }
}

/**
 * Unlocks the mutex.
 */
static inline void spin_unlock(spinlock_t *lock)
{
    if (lock) {
        __sync_synchronize();
        *lock = false;
    }
}

#endif // SPINLOCK_H
