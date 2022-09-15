// SPDX-License-Identifier: GPL-2.0
#ifndef CPULOCAL_H
#define CPULOCAL_H
/**
 * \file
 *
 * Provides functions to allocate and access thread-local flags.
 *
 *//*
 * Copyright (C) 2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

/**
 * A single thread-local flag. These are spaced out in memory to ensure each
 * flag occupies a different cache line.
 */
typedef struct __attribute__((packed)) {
    bool	flag;
    uint8_t	spacing[AP_STACK_SIZE - sizeof(bool)];
} local_flag_t;


/**
 * Allocates an array of thread-local flags, one per CPU core, and returns
 * a ID number that identifies the allocated array. Returns -1 if there is
 * insufficient thread local storage remaining to allocate a new array of
 * flags.
 */
static inline int allocate_local_flag(void)
{
    extern int local_bytes_used;
    if (local_bytes_used == LOCALS_SIZE) {
        return -1;
    }
    return local_bytes_used += sizeof(bool);
}


/**
 * Returns a pointer to the previously allocated array of thread-local flags
 * identified by flag_num.
 */
static inline local_flag_t *local_flags(int flag_num)
{
    // The number returned by allocate_local_flag is the byte offset of the
    // flag from the start of the thread-local storage.
    return (local_flag_t *)(_stacks + BSP_STACK_SIZE - LOCALS_SIZE + flag_num);
}

#endif // CPULOCAL_H
