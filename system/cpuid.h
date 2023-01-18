// SPDX-License-Identifier: GPL-2.0
#ifndef CPUID_H
#define CPUID_H
/**
 * \file
 *
 * Provides access to the CPUID information.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 * Copyright (C) 2004-2022 Sam Demeulemeester.
 *
 * Derived from memtest86+ cpuid.h
 */

#include <stdint.h>

#define CPU_ECORE_ID 0x20
#define CPU_PCORE_ID 0x40

typedef enum {
    CORE_UNKNOWN,
    CORE_PCORE,
    CORE_ECORE
} core_type_t;

/**
 * Structures that hold the collected CPUID information.
 */

typedef union {
    uint32_t        raw[2];
    struct {
        uint32_t    stepping        : 4;
        uint32_t    model           : 4;
        uint32_t    family          : 4;
        uint32_t    processorType   : 2;
        uint32_t                    : 2;
        uint32_t    extendedModel   : 4;
        uint32_t    extendedFamily  : 8;
        uint32_t                    : 4;
        uint32_t    extendedBrandID : 32; // AMD Only
    };
} cpuid_version_t;

typedef union {
    uint32_t        raw;
    struct {
        uint32_t    brandIndex              : 8;
        uint32_t    cflushLineSize          : 8;
        uint32_t    logicalProcessorCount   : 8;
        uint32_t    apicID                  : 8;
    };
} cpuid_proc_info_t;

typedef union {
    uint32_t        raw[3];
    struct {
        uint32_t    fpu     : 1;    // EDX feature flags, bit 0 */
        uint32_t    vme     : 1;
        uint32_t    de      : 1;
        uint32_t    pse     : 1;
        uint32_t    rdtsc   : 1;
        uint32_t    msr     : 1;
        uint32_t    pae     : 1;
        uint32_t    mce     : 1;
        uint32_t    cx8     : 1;
        uint32_t    apic    : 1;
        uint32_t            : 1;
        uint32_t    sep     : 1;
        uint32_t    mtrr    : 1;
        uint32_t    pge     : 1;
        uint32_t    mca     : 1;
        uint32_t    cmov    : 1;
        uint32_t    pat     : 1;
        uint32_t    pse36   : 1;
        uint32_t    psn     : 1;
        uint32_t    cflush  : 1;
        uint32_t            : 1;
        uint32_t    ds      : 1;
        uint32_t    acpi    : 1;
        uint32_t    mmx     : 1;
        uint32_t    fxsr    : 1;
        uint32_t    sse     : 1;
        uint32_t    sse2    : 1;
        uint32_t    ss      : 1;
        uint32_t    htt     : 1;
        uint32_t    tm      : 1;
        uint32_t    bit30   : 1;
        uint32_t    pbe     : 1;    // EDX feature flags, bit 31
        uint32_t    sse3    : 1;    // ECX feature flags, bit 0
        uint32_t    mulq    : 1;
        uint32_t    bit2    : 1;
        uint32_t    mon     : 1;
        uint32_t    dscpl   : 1;
        uint32_t    vmx     : 1;
        uint32_t    smx     : 1;
        uint32_t    eist    : 1;
        uint32_t    tm2     : 1;
        uint32_t            : 12;   // ECX feature flags, bit 20
        uint32_t    x2apic  : 1;
        uint32_t            : 10;   // ECX feature flags, bit 31
        uint32_t            : 19;   // EDX extended feature flags, bit 0
        uint32_t    nx      : 1;
        uint32_t            : 9;
        uint32_t    lm      : 1;
        uint32_t            : 2;    // EDX extended feature flags, bit 31
    };
} cpuid_feature_flags_t;

#define CPUID_VENDOR_LENGTH     3
#define CPUID_VENDOR_STR_LENGTH (CPUID_VENDOR_LENGTH * sizeof(uint32_t) + 1)    // includes space for null terminator

typedef union {
    uint32_t        raw[CPUID_VENDOR_LENGTH];
    char            str[CPUID_VENDOR_STR_LENGTH];
} cpuid_vendor_string_t;

#define CPUID_BRAND_LENGTH      12
#define CPUID_BRAND_STR_LENGTH  (CPUID_BRAND_LENGTH * sizeof(uint32_t) + 1)     // includes space for null terminator

typedef union {
    uint32_t        raw[CPUID_BRAND_LENGTH];
    char            str[CPUID_BRAND_STR_LENGTH];
} cpuid_brand_string_t;

typedef union {
    uint32_t        raw[4];
    struct {
        uint32_t                : 24;
        uint32_t    l1_i_size   : 8;
        uint32_t                : 24;
        uint32_t    l1_d_size   : 8;
        uint32_t                : 16;
        uint32_t    l2_size     : 16;
        uint32_t                : 18;
        uint32_t    l3_size     : 14;
    };
} cpuid_cache_info_t;

typedef union {
    uint32_t        raw;
    struct {
        uint32_t    : 1;
    };
} cpuid_custom_features;

typedef struct {
    int     core_count;
    int     thread_count;
    int     is_hybrid;
    int     ecore_count;
    int     pcore_count;
} cpuid_topology_t;

typedef struct {
    uint32_t                max_cpuid;
    uint32_t                max_xcpuid;
    uint32_t                dts_pmp;
    cpuid_version_t         version;
    cpuid_proc_info_t       proc_info;
    cpuid_feature_flags_t   flags;
    cpuid_vendor_string_t   vendor_id;
    cpuid_brand_string_t    brand_id;
    cpuid_cache_info_t      cache_info;
    cpuid_custom_features   custom;
    cpuid_topology_t        topology;
} cpuid_info_t;

typedef union {
    uint32_t        raw;
    struct {
        uint32_t    ctype                   : 5;
        uint32_t    level                   : 3;
        uint32_t    is_self_initializing    : 1;
        uint32_t    is_fully_associative    : 1;
        uint32_t    reserved                : 4;
        uint32_t    num_threads_sharing     : 12;
        uint32_t    num_cores_on_die        : 6;
    };
} cpuid4_eax_t;

typedef union {
    uint32_t        raw;
    struct {
        uint32_t    coherency_line_size     : 12;
        uint32_t    physical_line_partition : 10;
        uint32_t    ways_of_associativity   : 10;
    };
} cpuid4_ebx_t;

typedef union {
    uint32_t        raw;
    struct {
        uint32_t    number_of_sets          : 32;
    };
} cpuid4_ecx_t;

/**
 * The CPUID information collected by cpuid_init();
 */
extern cpuid_info_t cpuid_info;

/**
 * Reads the CPUID information and stores it in cpuid_info.
 */
void cpuid_init(void);

/**
 * Return the Core Type (for Hybrid CPUs)
 */
core_type_t get_ap_hybrid_type(void);

/**
 * Executes the cpuid instruction.
 */
static inline void cpuid(uint32_t op, uint32_t count, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    *eax = op;
    *ecx = count;
    __asm__ __volatile__ ("cpuid"
        : "=a" (*eax),
          "=b" (*ebx),
          "=c" (*ecx),
          "=d" (*edx)
        : "0"  (*eax),
          "2"  (*ecx)
    );
}

#endif // CPUID_H
