// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from an extract of memtest86+ test.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// Thanks to Passmark for calculate_chunk() and various comments !
// ----------------------------------------------------
// test.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_block_move(int my_cpu, int iterations)
{
    int ticks = 0;

    if (my_cpu == master_cpu) {
        display_test_pattern_name("block move");
    }

    // Initialize memory with the initial pattern.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, i, 16 * sizeof(testword_t));
        if ((end - start) < 15) SKIP_RANGE(1)  // we need at least 16 words for this test

        testword_t *p  = start;
        testword_t *pe = start;

        bool at_end = false;
        do {
            // take care to avoid pointer overflow
            if ((end - pe) >= SPIN_SIZE) {
                pe += SPIN_SIZE - 1;
            } else {
                at_end = true;
                pe = end;
            }
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)p;
            testword_t pattern1 = 1;
            do {
                testword_t pattern2 = ~pattern1;
                write_word(p + 0,  pattern1);
                write_word(p + 1,  pattern1);
                write_word(p + 2,  pattern1);
                write_word(p + 3,  pattern1);
                write_word(p + 4,  pattern2);
                write_word(p + 5,  pattern2);
                write_word(p + 6,  pattern1);
                write_word(p + 7,  pattern1);
                write_word(p + 8,  pattern1);
                write_word(p + 9,  pattern1);
                write_word(p + 10, pattern2);
                write_word(p + 11, pattern2);
                write_word(p + 12, pattern1);
                write_word(p + 13, pattern1);
                write_word(p + 14, pattern2);
                write_word(p + 15, pattern2);
                pattern1 = pattern1 << 1 | pattern1 >> (TESTWORD_WIDTH - 1);  // rotate left
            } while (p <= (pe - 16) && (p += 16)); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }
    flush_caches(my_cpu);

    // Now move the data around. First move the data up half of the segment size
    // we are testing. Then move the data to the original location + 32 bytes.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, i, 16 * sizeof(testword_t));
        if ((end - start) < 15) SKIP_RANGE(iterations)  // we need at least 16 words for this test

        testword_t *p  = start;
        testword_t *pe = start;
        bool at_end = false;
        do {
            // take care to avoid pointer overflow
            if ((end - pe) >= SPIN_SIZE) {
                pe += SPIN_SIZE - 1;
            } else {
                at_end = true;
                pe = end;
            }

            size_t half_length = (pe - p + 1) / 2;
            testword_t *pm = p + half_length;

            for (int j = 0; j < iterations; j++) {
                ticks++;
                if (my_cpu < 0) {
                    continue;
                }
                test_addr[my_cpu] = (uintptr_t)p;
#ifdef __x86_64__
                __asm__ __volatile__ (
                    "cld\n"
                    "jmp L110\n\t"

                    ".p2align 4,,7\n\t"
                    "L110:\n\t"

                    // At the end of all this
                    // - the second half equals the initial value of the first half
                    // - the first half is right shifted 64-bytes (with wrapping)

                    // Move first half to second half
                    "movq %1,%%rdi\n\t"     // Destination, pm (mid point)
                    "movq %0,%%rsi\n\t"     // Source, p (start point)
                    "movq %2,%%rcx\n\t"     // Length, half_length (size of a half in DWORDS)
                    "rep\n\t"
                    "movsq\n\t"

                    // Move the second half, less the last 64 bytes, to the first half, offset plus 64 bytes
                    "movq %0,%%rdi\n\t"
                    "addq $64,%%rdi\n\t"    // Destination, p (start-point) plus 32 bytes
                    "movq %1,%%rsi\n\t"     // Source, pm (mid-point)
                    "movq %2,%%rcx\n\t"
                    "subq $8,%%rcx\n\t"     // Length, half_length (size of a half in QWORDS) minus 8 QWORDS (64 bytes)
                    "rep\n\t"
                    "movsq\n\t"

                    // Move last 8 QWORDS (64 bytes) of the second half to the start of the first half
                    "movq %0,%%rdi\n\t"     // Destination, p(start-point)
                                            // Source, 8 QWORDS from the end of the second half, left over by the last rep/movsl
                    "movq $8,%%rcx\n\t"     // Length, 8 QWORDS (64 bytes)
                    "rep\n\t"
                    "movsq\n\t"

                    :: "g" (p), "g" (pm), "g" (half_length)
                    : "rdi", "rsi", "rcx"
                );
#else
                __asm__ __volatile__ (
                    "cld\n"
                    "jmp L110\n\t"

                    ".p2align 4,,7\n\t"
                    "L110:\n\t"

                    // At the end of all this
                    // - the second half equals the initial value of the first half
                    // - the first half is right shifted 32 bytes (with wrapping)

                    // Move first half to second half
                    "movl %1,%%edi\n\t"     // Destination, pm (mid point)
                    "movl %0,%%esi\n\t"     // Source, p (start point)
                    "movl %2,%%ecx\n\t"     // Length, half_length (size of a half in DWORDS)
                    "rep\n\t"
                    "movsl\n\t"

                    // Move the second half, less the last 32 bytes, to the first half, offset plus 32 bytes
                    "movl %0,%%edi\n\t"
                    "addl $32,%%edi\n\t"    // Destination, p (start-point) plus 32 bytes
                    "movl %1,%%esi\n\t"     // Source, pm (mid-point)
                    "movl %2,%%ecx\n\t"
                    "subl $8,%%ecx\n\t"     // Length, half_length (size of a half in DWORDS) minus 8 DWORDS (32 bytes)
                    "rep\n\t"
                    "movsl\n\t"

                    // Move last 8 DWORDS (32 bytes) of the second half to the start of the first half
                    "movl %0,%%edi\n\t"     // Destination, p(start-point)
                                            // Source, 8 DWORDS from the end of the second half, left over by the last rep/movsl
                    "movl $8,%%ecx\n\t"     // Length, 8 DWORDS (32 bytes)
                    "rep\n\t"
                    "movsl\n\t"

                    :: "g" (p), "g" (pm), "g" (half_length)
                    : "edi", "esi", "ecx"
                );
#endif
                do_tick(my_cpu);
                BAILOUT;
            }
        } while (!at_end && ++pe); // advance pe to next start point
    }

    flush_caches(my_cpu);

    // Now check the data. The error checking is rather crude.  We just check that the
    // adjacent words are the same.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, i, 16 * sizeof(testword_t));
        if ((end - start) < 15) SKIP_RANGE(1)  // we need at least 16 words for this test

        testword_t *p  = start;
        testword_t *pe = start;

        bool at_end = false;
        do {
            // take care to avoid pointer overflow
            if ((end - pe) >= SPIN_SIZE) {
                pe += SPIN_SIZE - 1;
            } else {
                at_end = true;
                pe = end;
            }
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)p;
            do {
                testword_t p0 = read_word(p + 0);
                testword_t p1 = read_word(p + 1);
                if (unlikely(p0 != p1)) {
                    data_error(p, p0, p1, false);
                }
            } while (p <= (pe - 2) && (p += 2)); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    return ticks;
}
