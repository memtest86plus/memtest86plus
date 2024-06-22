// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.

#include <stdbool.h>
#include <stddef.h>

#include "cpulocal.h"
#include "smp.h"

#include "assert.h"

#include "barrier.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void barrier_init(barrier_t *barrier, int num_threads)
{
    barrier->flag_num = allocate_local_flag();
    assert(barrier->flag_num >= 0);

    barrier_reset(barrier, num_threads);
}

void barrier_reset(barrier_t *barrier, int num_threads)
{
    barrier->num_threads = num_threads;
    barrier->count       = num_threads;

    local_flag_t *waiting_flags = local_flags(barrier->flag_num);
    for (int cpu_num = 0; cpu_num < num_available_cpus; cpu_num++) {
        waiting_flags[cpu_num].flag = false;
    }
}

void barrier_spin_wait(barrier_t *barrier)
{
    if (barrier == NULL || barrier->num_threads < 2) {
        return;
    }
    local_flag_t *waiting_flags = local_flags(barrier->flag_num);
    int my_cpu = smp_my_cpu_num();
    waiting_flags[my_cpu].flag = true;
    if (__sync_sub_and_fetch(&barrier->count, 1) != 0) {
        volatile bool *i_am_blocked = &waiting_flags[my_cpu].flag;
        while (*i_am_blocked) {
#if defined(__x86_64) || defined(__i386__)
            __builtin_ia32_pause();
#elif defined (__loongarch_lp64)
            __asm__ __volatile__ (
              "nop \n\t" \
              "nop \n\t" \
              "nop \n\t" \
              "nop \n\t" \
              "nop \n\t" \
              "nop \n\t" \
              "nop \n\t" \
              "nop \n\t" \
            );
#endif
        }
        return;
    }
    // Last one here, so reset the barrier and wake the others. No need to
    // check if a CPU core is actually waiting - just clear all the flags.
    barrier->count = barrier->num_threads;
    __sync_synchronize();
    for (int cpu_num = 0; cpu_num < num_available_cpus; cpu_num++) {
        waiting_flags[cpu_num].flag = false;
    }
}

void barrier_halt_wait(barrier_t *barrier)
{
    if (barrier == NULL || barrier->num_threads < 2) {
        return;
    }
    local_flag_t *waiting_flags = local_flags(barrier->flag_num);
    int my_cpu = smp_my_cpu_num();
    waiting_flags[my_cpu].flag = true;
    //
    // There is a small window of opportunity for the wakeup signal to arrive
    // between us decrementing the barrier count and halting. So code the
    // following in assembler, both to ensure the window of opportunity is as
    // small as possible, and also to allow us to detect and skip over the
    // halt in the interrupt handler.
    //
    // if (__sync_sub_and_fetch(&barrier->count, 1) != 0) {
    //     __asm__ __volatile__ ("hlt");
    //     return;
    // }
    //
#if defined(__i386__) || defined(__x86_64__)
    __asm__ goto ("\t"
        "lock decl %0 \n\t"
        "je 0f        \n\t"
        "hlt          \n\t"
        "jmp %l[end]  \n"
        "0:           \n"
        : /* no outputs */
        : "m" (barrier->count)
        : /* no clobbers */
        : end
    );
#elif defined(__loongarch_lp64)
    __asm__ goto ("\t"
        "li.w $t0, -1\n\t" \
        "li.w $t2, 1\n\t" \
        "amadd_db.w $t1, $t0, %0\n\t" \
        "bge $t2, $t1, 0f\n\t" \
        "1:          \n\t" \
        "idle 0x0\n\t" \
        "b    1b\n\t" \
        "bl %l[end]\n\t" \
        "0:\n\t" \
        : /* no outputs */
        : "r" (&(barrier->count))
        : "$t0", "t1", "$t2"
        : end
    );
#endif
    // Last one here, so reset the barrier and wake the others.
    barrier->count = barrier->num_threads;
    __sync_synchronize();
    waiting_flags[my_cpu].flag = false;
    for (int cpu_num = 0; cpu_num < num_available_cpus; cpu_num++) {
        if (waiting_flags[cpu_num].flag) {
            waiting_flags[cpu_num].flag = false;
            smp_send_nmi(cpu_num);
        }
    }
end:
    return;
}
