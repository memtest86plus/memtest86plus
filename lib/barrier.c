// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.
//
// Derived from an extract of memtest86+ smp.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// ------------------------------------------------
// smp.c - MemTest-86  Version 3.5
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stddef.h>

#include "barrier.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void barrier_init(barrier_t *barrier, int num_threads)
{
    barrier->num_threads = num_threads;
    barrier->count = num_threads;
    spin_unlock(&barrier->lock);
    spin_unlock(&barrier->st1);
    spin_unlock(&barrier->st2);
    spin_lock(&barrier->st2);
}

void barrier_wait(barrier_t *barrier)
{
    if (barrier == NULL || barrier->num_threads < 2) {
        return;
    }
    spin_wait(&barrier->st1);            // Wait if the barrier is active.
    spin_lock(&barrier->lock);           // Get lock for barrier struct.
    if (--barrier->count == 0) {         // Last process?
        spin_lock(&barrier->st1);        // Hold up any processes re-entering.
        spin_unlock(&barrier->st2);      // Release the other processes.
        barrier->count++;
        spin_unlock(&barrier->lock);
    } else {
        spin_unlock(&barrier->lock);
        spin_wait(&barrier->st2);        // Wait for peers to arrive.
        spin_lock(&barrier->lock);
        if (++barrier->count == barrier->num_threads) {
            spin_unlock(&barrier->st1);
            spin_lock(&barrier->st2);
        }
        spin_unlock(&barrier->lock);
    }
}
