// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
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
#include "config.h"

#include "test_funcs.h"
#include "test_helper.h"

#define HAND_OPTIMISED  1   // Use hand-optimised assembler code for performance.

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

#if defined(__i386__) || defined(__x86_64__)

#include "memrw_simd.h"

// ==================== vvvvv MMX vvvvv ====================

#if 0
// XXX how to make this work without producing GCC error: 'SSE register return with SSE disabled' ?
#pragma GCC target ("mmx", "no-sse", "no-sse2", "no-avx")
static __attribute__((noinline)) testword_t * write_loops_simd64(testword_t *p, testword_t *pe, testword_t pattern1)
{
    register __m64 mdpattern1 __asm__("%mm0") = convert_testword_to_simd64(pattern1);
    if (enable_nontemporal) {
        do {
            write64nt_simd((__m64 *)p, mdpattern1);
            p += (sizeof(*p) < 8) ? 1 : 0;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    else {
        do {
            write64_simd((__m64 *)p, mdpattern1);
            p += (sizeof(*p) < 8) ? 1 : 0;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    __asm__ __volatile__ ("emms");
    __sync_synchronize();
    return p;
}
#endif

#pragma GCC target ("mmx", "no-sse", "no-sse2", "no-avx")
static __attribute__((noinline)) testword_t * write_loops_simd64_mmx(testword_t *p, testword_t *pe, testword_t pattern1)
{
    if (enable_nontemporal) {
        do {
            write64nt_mmx(p, pattern1);
            p += (sizeof(*p) < 8) ? 1 : 0;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    else {
        do {
            write64_mmx(p, pattern1);
            p += (sizeof(*p) < 8) ? 1 : 0;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    __asm__ __volatile__ ("emms");
    __sync_synchronize();
    return p;
}

// ? TODO ? read1_loops_simd64
// ? TODO ? read2_loops_simd64

// ==================== ^^^^^ MMX ^^^^^ ====================


// ==================== vvvvv SSE vvvvv ====================

#pragma GCC target ("sse", "no-sse2", "no-avx")
static __attribute__((noinline)) testword_t * write_loops_simd128_sse(testword_t *p, testword_t *pe, testword_t pattern1)
{
    __m128 mdpattern1 = convert_testword_to_simd128_sse(pattern1);
    if (enable_nontemporal) {
        do {
            write128nt_sse((__m128 *)p, mdpattern1);
            p += (sizeof(*p) < 8) ? 3 : 1;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    else {
        do {
            write128_sse((__m128 *)p, mdpattern1);
            p += (sizeof(*p) < 8) ? 3 : 1;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    __sync_synchronize();
    return p;
}

#define COMPARE_TARGET 0xF

#pragma GCC target ("sse", "no-sse2", "no-avx")
static __attribute__((noinline)) testword_t * read1_loops_simd128_sse(testword_t *p, testword_t *pe, testword_t pattern1, testword_t pattern2)
{
    __m128 mdpattern1 = convert_testword_to_simd128_sse(pattern1);
    __m128 mdpattern2 = convert_testword_to_simd128_sse(pattern2);
    do {
        __m128 actual = read128_sse((__m128 *)p);
        int compar_result = compare128_sse(mdpattern1, actual);
        write128_sse((__m128 *)p, mdpattern2);
        if (unlikely(compar_result != COMPARE_TARGET)) {
            __m128 good = mdpattern1;
            __m128 bad = actual;
            data_error_wide(p, (testword_t *)&good, (testword_t *)&bad, 128 / (8 * sizeof(*p)), true);
        }
        p += (sizeof(*p) < 8) ? 3 : 1;
    } while (p++ < pe); // test before increment in case pointer overflows
    return p;
}

#pragma GCC target ("sse", "no-sse2", "no-avx")
static __attribute__((noinline)) testword_t * read2_loops_simd128_sse(testword_t *p, testword_t *ps, testword_t pattern1, testword_t pattern2)
{
    __m128 mdpattern1 = convert_testword_to_simd128_sse(pattern1);
    __m128 mdpattern2 = convert_testword_to_simd128_sse(pattern2);
    do {
        __m128 actual = read128_sse((__m128 *)p);
        int compar_result = compare128_sse(mdpattern2, actual);
        write128_sse((__m128 *)p, mdpattern1);
        if (unlikely(compar_result != COMPARE_TARGET)) {
            __m128 good = mdpattern2;
            __m128 bad = actual;
            data_error_wide(p, (testword_t *)&good, (testword_t *)&bad, 128 / (8 * sizeof(*p)), true);
        }
        p -= (sizeof(*p) < 8) ? 3 : 1;
    } while (p-- > ps); // test before decrement in case pointer overflows
    return p;
}

#undef COMPARE_TARGET

// ==================== ^^^^^ SSE ^^^^^ ====================


// ==================== vvvvv SSE2 vvvvv ====================

#pragma GCC target ("sse2", "no-avx")
static __attribute__((noinline)) testword_t * write_loops_simd128_sse2(testword_t *p, testword_t *pe, testword_t pattern1)
{
    __m128 mdpattern1 = convert_testword_to_simd128_sse2(pattern1);
    if (enable_nontemporal) {
        do {
            write128nt_sse2((__m128 *)p, mdpattern1);
            p += (sizeof(*p) < 8) ? 3 : 1;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    else {
        do {
            write128_sse2((__m128 *)p, mdpattern1);
            p += (sizeof(*p) < 8) ? 3 : 1;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    __sync_synchronize();
    return p;
}

#define COMPARE_TARGET 0x3

#pragma GCC target ("sse2", "no-avx")
static __attribute__((noinline)) testword_t * read1_loops_simd128_sse2(testword_t *p, testword_t *pe, testword_t pattern1, testword_t pattern2)
{
    __m128 mdpattern1 = convert_testword_to_simd128_sse2(pattern1);
    __m128 mdpattern2 = convert_testword_to_simd128_sse2(pattern2);
    do {
        __m128 actual = read128_sse2((__m128 *)p);
        int compar_result = compare128_sse2(mdpattern1, actual);
        write128_sse2((__m128 *)p, mdpattern2);
        if (unlikely(compar_result != COMPARE_TARGET)) {
            __m128 good = mdpattern1;
            __m128 bad = actual;
            data_error_wide(p, (testword_t *)&good, (testword_t *)&bad, 128 / (8 * sizeof(*p)), true);
        }
        p += (sizeof(*p) < 8) ? 3 : 1;
    } while (p++ < pe); // test before increment in case pointer overflows
    return p;
}

#pragma GCC target ("sse2", "no-avx")
static __attribute__((noinline)) testword_t * read2_loops_simd128_sse2(testword_t *p, testword_t *ps, testword_t pattern1, testword_t pattern2)
{
    __m128 mdpattern1 = convert_testword_to_simd128_sse2(pattern1);
    __m128 mdpattern2 = convert_testword_to_simd128_sse2(pattern2);
    do {
        __m128 actual = read128_sse2((__m128 *)p);
        int compar_result = compare128_sse2(mdpattern2, actual);
        write128_sse2((__m128 *)p, mdpattern1);
        if (unlikely(compar_result != COMPARE_TARGET)) {
            __m128 good = mdpattern2;
            __m128 bad = actual;
            data_error_wide(p, (testword_t *)&good, (testword_t *)&bad, 128 / (8 * sizeof(*p)), true);
        }
        p -= (sizeof(*p) < 8) ? 3 : 1;
    } while (p-- > ps); // test before decrement in case pointer overflows
    return p;
}

#undef COMPARE_TARGET

// ==================== ^^^^^ SSE2 ^^^^^ ====================


// ==================== vvvvv AVX vvvvv ====================

#define COMPARE_TARGET 0xF

#pragma GCC target ("avx")
static __attribute__((noinline)) testword_t * write_loops_simd256_avx(testword_t *p, testword_t *pe, testword_t pattern1)
{
    __m256 mdpattern1 = convert_testword_to_simd256_avx(pattern1);
    if (enable_nontemporal) {
        do {
            write256nt_avx((__m256 *)p, mdpattern1);
            p += (sizeof(*p) < 8) ? 7 : 3;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    else {
        do {
            write256_avx((__m256 *)p, mdpattern1);
            p += (sizeof(*p) < 8) ? 7 : 3;
        } while (p++ < pe); // test before increment in case pointer overflows
    }
    __sync_synchronize();
    return p;
}

#pragma GCC target ("avx")
static __attribute__((noinline)) testword_t * read1_loops_simd256_avx(testword_t *p, testword_t *pe, testword_t pattern1, testword_t pattern2)
{
    __m256 mdpattern1 = convert_testword_to_simd256_avx(pattern1);
    __m256 mdpattern2 = convert_testword_to_simd256_avx(pattern2);
    do {
        __m256 actual = read256_avx((__m256 *)p);
        int compar_result = compare256_avx(mdpattern1, actual);
        write256_avx((__m256 *)p, mdpattern2);
        if (unlikely(compar_result != COMPARE_TARGET)) {
            __m256 good = mdpattern1;
            __m256 bad = actual;
            data_error_wide(p, (testword_t *)&good, (testword_t *)&bad, 256 / (8 * sizeof(*p)), true);
        }
        p += (sizeof(*p) < 8) ? 7 : 3;
    } while (p++ < pe); // test before increment in case pointer overflows
    return p;
}

#pragma GCC target ("avx")
static __attribute__((noinline)) testword_t * read2_loops_simd256_avx(testword_t *p, testword_t *ps, testword_t pattern1, testword_t pattern2)
{
    __m256 mdpattern1 = convert_testword_to_simd256_avx(pattern1);
    __m256 mdpattern2 = convert_testword_to_simd256_avx(pattern2);
    do {
        __m256 actual = read256_avx((__m256 *)p);
        int compar_result = compare256_avx(mdpattern2, actual);
        write256_avx((__m256 *)p, mdpattern1);
        if (unlikely(compar_result != COMPARE_TARGET)) {
            __m256 good = mdpattern2;
            __m256 bad = actual;
            data_error_wide(p, (testword_t *)&good, (testword_t *)&bad, 256 / (8 * sizeof(*p)), true);
        }
        p -= (sizeof(*p) < 8) ? 7 : 3;
    } while (p-- > ps); // test before decrement in case pointer overflows
    return p;
}

#undef COMPARE_TARGET

// ==================== ^^^^^ AVX ^^^^^ ====================

#endif

int test_mov_inv_fixed(int my_cpu, int iterations, testword_t pattern1, testword_t pattern2, int simd)
{
    int ticks = 0;

    size_t chunk_align = simd == 1 ? 64/8 : ((simd == 2 || simd == 3) ? 128/8 : (simd == 4 ? 256/8 : sizeof(testword_t)));
    if (my_cpu == master_cpu) {
        display_test_pattern_values(pattern1, simd);
    }

    // Initialize memory with the initial pattern.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, i, chunk_align);
        __asm__ volatile("nop");
        if (end < start) SKIP_RANGE(1) // we need enough words for this test

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
            //do_trace(0, "W  p  %016x -> pe %016x", (uintptr_t)p, (uintptr_t) pe);
            test_addr[my_cpu] = (uintptr_t)p;
            if (!simd || ((end - start) < (int)((32/8 << simd) / sizeof(testword_t)) - 1)) {
#if HAND_OPTIMISED
#if defined(__x86_64__)
                uint64_t length = pe - p + 1;
                __asm__  __volatile__ ("\t"
                    "rep    \n\t"
                    "stosq  \n\t"
                    :
                    : "c" (length), "D" (p), "a" (pattern1)
                    :
                );
                p = pe;
#elif defined(__i386__)
                uint32_t length = pe - p + 1;
                __asm__  __volatile__ ("\t"
                    "rep    \n\t"
                    "stosl  \n\t"
                    :
                    : "c" (length), "D" (p), "a" (pattern1)
                    :
                );
                p = pe;
#elif defined(__loongarch_lp64)
                uint64_t length = pe - p + 1;
                __asm__  __volatile__ ("\t"
                    "loop:               \n\t"
                    "st.d %2, %1, 0x0    \n\t"
                    "addi.d %1, %1, 0x8  \n\t"
                    "addi.d %0, %0, -0x1 \n\t"
                    "bnez %0, loop       \n\t"
                    :
                    : "r" (length), "r" (p), "r" (pattern1)
                    : "memory"
                );
                p = pe;
#endif
#else
                do {
                    write_word(p, pattern1);
                } while (p++ < pe); // test before increment in case pointer overflows
#endif
            }
// SIMD code paths
#if defined(__i386__) || defined(__x86_64__)
            else if (simd == 1) {
                p = write_loops_simd64_mmx(p, pe, pattern1);
            }
            else if (simd == 2) {
                p = write_loops_simd128_sse(p, pe, pattern1);
            }
            else if (simd == 3) {
                p = write_loops_simd128_sse2(p, pe, pattern1);
            }
            else if (simd == 4) {
                p = write_loops_simd256_avx(p, pe, pattern1);
            }
#endif
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    // Check for the current pattern and then write the alternate pattern for
    // each memory location. Test from the bottom up and then from the top down.
    for (int i = 0; i < iterations; i++) {
        flush_caches(my_cpu);

        for (int j = 0; j < vm_map_size; j++) {
            testword_t *start, *end;
            calculate_chunk(&start, &end, my_cpu, j, chunk_align);
            if (end < start) SKIP_RANGE(1) // we need enough words for this test

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
                //do_trace(0, "R1 p  %016x -> pe %016x", (uintptr_t)p, (uintptr_t)pe);
                test_addr[my_cpu] = (uintptr_t)p;
                if (simd == 0 || simd == 1 || ((end - start) < (int)((32/8 << simd) / sizeof(testword_t)) - 1)) {
                    do {
                        testword_t actual = read_word(p);
                        if (unlikely(actual != pattern1)) {
                            data_error(p, pattern1, actual, true);
                        }
                        write_word(p, pattern2);
                    } while (p++ < pe); // test before increment in case pointer overflows
                }
// SIMD code paths
#if defined(__i386__) || defined(__x86_64__)
                else if (simd == 2) {
                    p = read1_loops_simd128_sse(p, pe, pattern1, pattern2);
                }
                else if (simd == 3) {
                    p = read1_loops_simd128_sse2(p, pe, pattern1, pattern2);
                }
                else if (simd == 4) {
                    p = read1_loops_simd256_avx(p, pe, pattern1, pattern2);
                }
#endif
                do_tick(my_cpu);
                BAILOUT;
            } while (!at_end && ++pe); // advance pe to next start point
        }

        flush_caches(my_cpu);

        for (int j = vm_map_size - 1; j >= 0; j--) {
            testword_t *start, *end;
            calculate_chunk(&start, &end, my_cpu, j, chunk_align);
            if (end < start) SKIP_RANGE(1) // we need enough words for this test

            testword_t *p  = end;
            testword_t *ps = end;

            bool at_start = false;
            do {
                // take care to avoid pointer underflow
                if ((ps - start) >= SPIN_SIZE) {
                    ps -= SPIN_SIZE - 1;
                } else {
                    at_start = true;
                    ps = start;
                }
                ticks++;
                if (my_cpu < 0) {
                    continue;
                }
                //do_trace(0, "R2 ps %016x -> p  %016x", (uintptr_t)ps, (uintptr_t)p);
                test_addr[my_cpu] = (uintptr_t)p;
                if (simd == 0 || simd == 1 || ((end - start) < (int)((32/8 << simd) / sizeof(testword_t)) - 1)) {
                    do {
                        testword_t actual = read_word(p);
                        if (unlikely(actual != pattern2)) {
                            data_error(p, pattern2, actual, true);
                        }
                        write_word(p, pattern1);
                    } while (p-- > ps); // test before decrement in case pointer overflows
                }
// SIMD code paths
#if defined(__i386__) || defined(__x86_64__)
                else if (simd == 2) {
                    p = read2_loops_simd128_sse((testword_t *)((uintptr_t)p & ~(uintptr_t)0xF), ps, pattern1, pattern2);
                }
                else if (simd == 3) {
                    p = read2_loops_simd128_sse2((testword_t *)((uintptr_t)p & ~(uintptr_t)0xF), ps, pattern1, pattern2);
                }
                else if (simd == 4) {
                    p = read2_loops_simd256_avx((testword_t *)((uintptr_t)p & ~(uintptr_t)0x1F), ps, pattern1, pattern2);
                }
#endif
                do_tick(my_cpu);
                BAILOUT;
            } while (!at_start && --ps); // advance ps to next start point
        }
    }

    return ticks;
}
