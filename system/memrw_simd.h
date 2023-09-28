// SPDX-License-Identifier: GPL-2.0
#ifndef MEMRW_SIMD_H
#define MEMRW_SIMD_H

#if defined(__i386__) || defined(__x86_64__)

#include <immintrin.h>

// -------------------- COMMON TO x86 & x86-64 --------------------

/**
 * Reads and returns the value stored in the 128-bit memory location pointed to by ptr.
 */
#pragma GCC target ("sse")
static inline __m128 read128_sse(const volatile __m128 *ptr)
{
    __m128 val;
    __asm__ __volatile__(
        "movaps %1, %0"
        : "=x" (val)
        : "m" (*ptr)
        : "memory"
    );
    return val;
}

/**
 * Writes val to the 128-bit memory location pointed to by ptr, using SSE register as source operand.
 */
#pragma GCC target ("sse")
static inline void write128_sse(const volatile __m128 *ptr, __m128 val)
{
    __asm__ __volatile__(
        "movaps %1, %0"
        :
        : "m" (*ptr),
          "x" (val)
        : "memory"
    );
}

/**
 * Writes val to the 128-bit memory location pointed to by ptr, using SSE register as source operand, using non-temporal hint.
 */
#pragma GCC target ("sse")
static inline void write128nt_sse(const volatile __m128 *ptr, __m128 val)
{
    __asm__ __volatile__(
        "movntps %1, %0"
        :
        : "m" (*ptr),
          "x" (val)
        : "memory"
    );
}

#pragma GCC target ("sse")
static inline int compare128_sse(__m128 val1, __m128 val2)
{
    return _mm_movemask_ps(_mm_cmpeq_ps(val1, val2));
}

/**
 * Writes val to the 128-bit memory location pointed to by ptr, using SSE register as source operand.
 */
#pragma GCC target ("sse2")
static inline void write128_sse2(const volatile __m128 *ptr, __m128 val)
{
    __asm__ __volatile__(
        "movdqa %1, %0"
        :
        : "m" (*ptr),
          "x" (val)
        : "memory"
    );
}

/**
 * Writes val to the 128-bit memory location pointed to by ptr, using SSE register as source operand, using non-temporal hint.
 */
#pragma GCC target ("sse2")
static inline void write128nt_sse2(const volatile __m128 *ptr, __m128 val)
{
    __asm__ __volatile__(
        "movntdq %1, %0"
        :
        : "m" (*ptr),
          "x" (val)
        : "memory"
    );
}

/**
 * Reads and returns the value stored in the 128-bit memory location pointed to by ptr.
 */
#pragma GCC target ("sse2")
static inline __m128 read128_sse2(const volatile __m128 *ptr)
{
    __m128 val;
    __asm__ __volatile__(
        "movdqa %1, %0"
        : "=x" (val)
        : "m" (*ptr)
        : "memory"
    );
    return val;
}

#pragma GCC target ("sse2")
static inline int compare128_sse2(__m128 val1, __m128 val2)
{
    return _mm_movemask_pd(_mm_cmpeq_pd((__m128d)val1, (__m128d)val2));
}

/**
 * Reads and returns the value stored in the 128-bit memory location pointed to by ptr.
 */
#pragma GCC target ("avx")
static inline __m256 read256_avx(const volatile __m256 *ptr)
{
    __m256 val;
    __asm__ __volatile__(
        "vmovdqa %1, %0"
        : "=x" (val)
        : "m" (*ptr)
        : "memory"
    );
    return val;
}

/**
 * Writes val to the 256-bit memory location pointed to by ptr, using AVX register as source operand.
 */
#pragma GCC target ("avx")
static inline void write256_avx(const volatile __m256 *ptr, __m256 val)
{
    __asm__ __volatile__(
        "vmovdqa %1, %0"
        :
        : "m" (*ptr),
          "x" (val)
        : "memory"
    );
}

/**
 * Writes val to the 256-bit memory location pointed to by ptr, using AVX register as source operand, using non-temporal hint.
 */
#pragma GCC target ("avx")
static inline void write256nt_avx(const volatile __m256 *ptr, __m256 val)
{
    __asm__ __volatile__(
        "vmovntdq %1, %0"
        :
        : "m" (*ptr),
          "x" (val)
        : "memory"
    );
}

#pragma GCC target ("avx")
static inline int compare256_avx(__m256 val1, __m256 val2)
{
    return _mm256_movemask_pd(_mm256_cmp_pd((__m256d)val1, (__m256d)val2, 0));
}

#endif

// -------------------- SEPARATE FOR x86 & x86-64 --------------------

#if defined(__i386__)

#if 0
#pragma GCC target ("mmx", "no-sse", "no-sse2", "no-avx")
static inline __m64 convert_testword_to_simd64_mmx(uint32_t val)
{
    return _mm_set1_pi32(val);
}
#endif

/**
 * Writes val to the 64-bit memory location pointed to by ptr, using MMX register as source operand.
 */
#pragma GCC target ("mmx", "no-sse", "no-sse2", "no-avx")
static inline void write64_mmx(const volatile uint32_t *ptr, uint32_t val)
{
    __m64 val2 = _mm_set1_pi32(val);
    __asm__ __volatile__(
        "movq %1, %0"
        :
        : "m" (*ptr),
          "y" (val2)
        : "memory"
    );
}

/**
 * Writes val to the 64-bit memory location pointed to by ptr, using MMX register as source operand, using non-temporal hint.
 */
#pragma GCC target ("mmx", "no-sse", "no-sse2", "no-avx")
static inline void write64nt_mmx(const volatile uint32_t *ptr, uint32_t val)
{
    __m64 val2 = _mm_set1_pi32(val);
    __asm__ __volatile__(
        "movntq %1, %0"
        :
        : "m" (*ptr),
          "y" (val2)
        : "memory"
    );
}

#pragma GCC target ("sse")
static inline __m128 convert_testword_to_simd128_sse(uint32_t val)
{
    __attribute__((aligned(16))) float tmp[4];
    float * tmp2 = tmp;
    *(uint32_t *)tmp2++ = val;
    *(uint32_t *)tmp2++ = val;
    *(uint32_t *)tmp2++ = val;
    *(uint32_t *)tmp2++ = val;
    return _mm_load_ps(tmp);
}

#pragma GCC target ("sse2")
static inline __m128 convert_testword_to_simd128_sse2(uint32_t val)
{
    return (__m128)_mm_set1_epi32(val);
}

#pragma GCC target ("avx")
static inline __m256 convert_testword_to_simd256_avx(uint32_t val)
{
    return (__m256)_mm256_set1_epi32(val);
}

#elif defined(__x86_64__)

// XXX how to make this work without producing GCC error: 'SSE register return with SSE disabled' ?
#if 0
#pragma GCC target ("mmx", "no-sse", "no-sse2", "no-avx")
static inline __m64 convert_testword_to_simd64_mmx(uint64_t val)
{
    return (__m64)val;
}
#endif

/**
 * Writes val to the 64-bit memory location pointed to by ptr, using MMX register as source operand.
 */
#pragma GCC target ("mmx", "no-sse", "no-sse2", "no-avx")
static inline void write64_mmx(const volatile uint64_t *ptr, uint64_t val)
{
    __m64 val2 = (__m64)val;
    __asm__ __volatile__(
        "movq %1, %0"
        :
        : "m" (*ptr),
          "y" (val2)
        : "memory"
    );
}

/**
 * Writes val to the 64-bit memory location pointed to by ptr, using MMX register as source operand, using non-temporal hint.
 */
#pragma GCC target ("mmx", "no-sse", "no-sse2", "no-avx")
static inline void write64nt_mmx(const volatile uint64_t *ptr, uint64_t val)
{
    __m64 val2 = (__m64)val;
    __asm__ __volatile__(
        "movntq %1, %0"
        :
        : "m" (*ptr),
          "y" (val2)
        : "memory"
    );
}

#pragma GCC target ("sse")
static inline __m128 convert_testword_to_simd128_sse(uint64_t val)
{
    __attribute__((aligned(16))) float tmp[4];
    double * tmp2 = (double *)tmp;
    *(uint64_t *)tmp2++ = val;
    *(uint64_t *)tmp2++ = val;
    return _mm_load_ps(tmp);
}

#pragma GCC target ("sse2")
static inline __m128 convert_testword_to_simd128_sse2(uint64_t val)
{
    return (__m128)_mm_set1_epi64x(val);
}

#pragma GCC target ("avx")
static inline __m256 convert_testword_to_simd256_avx(uint64_t val)
{
    return (__m256)_mm256_set1_epi64x(val);
}

#endif // __i386__ or __x86_64__

#endif // MEMRW_SIMD_H
