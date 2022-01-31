// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from memtest86+ cpuid.h
// (original contained no copyright statement)

#include <stdint.h>

#include "cpuid.h"

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

cpuid_info_t cpuid_info;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void cpuid_init(void)
{
    uint32_t dummy[3];
    char *p, *q;

    // Get the max standard cpuid & vendor ID.
    cpuid(0x0, 0,
        &cpuid_info.max_cpuid,
        &cpuid_info.vendor_id.raw[0],
        &cpuid_info.vendor_id.raw[2],
        &cpuid_info.vendor_id.raw[1]
    );
    cpuid_info.vendor_id.str[CPUID_VENDOR_STR_LENGTH - 1] = '\0';

    // Get the processor family information & feature flags.
    if (cpuid_info.max_cpuid >= 1) {
        cpuid(0x1, 0,
            &cpuid_info.version.raw,
            &cpuid_info.proc_info.raw,
            &cpuid_info.flags.raw[1],
            &cpuid_info.flags.raw[0]
        );
    }

    // Get the digital thermal sensor & power management status bits.
    if (cpuid_info.max_cpuid >= 6) {
        cpuid(0x6, 0,
            &cpuid_info.dts_pmp,
            &dummy[0],
            &dummy[1],
            &dummy[2]
        );
    }

    // Get the max extended cpuid.
    cpuid(0x80000000, 0,
        &cpuid_info.max_xcpuid,
        &dummy[0],
        &dummy[1],
        &dummy[2]
    );

    // Get extended feature flags, only save EDX.
    if (cpuid_info.max_xcpuid >= 0x80000001) {
        cpuid(0x80000001, 0,
            &dummy[0],
            &dummy[1],
            &dummy[2],
            &cpuid_info.flags.raw[2]
        );
    }

    // Get the brand ID.
    if (cpuid_info.max_xcpuid >= 0x80000004) {
        cpuid(0x80000002, 0,
            &cpuid_info.brand_id.raw[0],
            &cpuid_info.brand_id.raw[1],
            &cpuid_info.brand_id.raw[2],
            &cpuid_info.brand_id.raw[3]
        );
        cpuid(0x80000003, 0,
            &cpuid_info.brand_id.raw[4],
            &cpuid_info.brand_id.raw[5],
            &cpuid_info.brand_id.raw[6],
            &cpuid_info.brand_id.raw[7]
        );
        cpuid(0x80000004, 0,
            &cpuid_info.brand_id.raw[8],
            &cpuid_info.brand_id.raw[9],
            &cpuid_info.brand_id.raw[10],
            &cpuid_info.brand_id.raw[11]
        );
        cpuid_info.brand_id.str[CPUID_BRAND_STR_LENGTH - 1] = '\0';
    }
    // Intel chips right-justify this string for some reason - undo that.
    p = q = &cpuid_info.brand_id.str[0];
    while (*p == ' ') {
        p++;
    }
    if (p != q) {
        while (*p) {
            *q++ = *p++;
        }
        while (q <= &cpuid_info.brand_id.str[CPUID_BRAND_STR_LENGTH]) {
            *q++ = '\0';
        }
    }

    // Get cache information.
    switch (cpuid_info.vendor_id.str[0]) {
      case 'A':
        // AMD Processors
        if (cpuid_info.max_xcpuid >= 0x80000005) {
            cpuid(0x80000005, 0,
                &dummy[0],
                &dummy[1],
                &cpuid_info.cache_info.raw[0],
                &cpuid_info.cache_info.raw[1]
            );
        }
        if (cpuid_info.max_xcpuid >= 0x80000006) {
            cpuid(0x80000006, 0,
                &dummy[0],
                &dummy[1],
                &cpuid_info.cache_info.raw[2],
                &cpuid_info.cache_info.raw[3]
            );
        }
        break;
      case 'G':
        // Intel Processors
        // No cpuid info to read.
        break;
    }
}
