// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2022 Sam Demeulemeester.
//
// Derived from memtest86+ cpuid.h

#include <stdbool.h>
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
    uint32_t reg[4];
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
            &cpuid_info.version.raw[0],
            &cpuid_info.proc_info.raw,
            &cpuid_info.flags.raw[1],
            &cpuid_info.flags.raw[0]
        );
    }

    // Get the digital thermal sensor & power management status bits.
    if (cpuid_info.max_cpuid >= 6) {
        cpuid(0x6, 0,
            &cpuid_info.dts_pmp,
            &reg[0],
            &reg[1],
            &reg[2]
        );
    }

    // Get the max extended cpuid.
    cpuid(0x80000000, 0,
        &cpuid_info.max_xcpuid,
        &reg[0],
        &reg[1],
        &reg[2]
    );

    // Get extended feature flags, only save EDX.
    if (cpuid_info.max_xcpuid >= 0x80000001) {
        cpuid(0x80000001, 0,
            &reg[0],
            &cpuid_info.version.raw[1],
            &reg[1],
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
    if (cpuid_info.max_xcpuid >= 0x80000005) {
        cpuid(0x80000005, 0,
            &reg[0],
            &reg[1],
            &cpuid_info.cache_info.raw[0],
            &cpuid_info.cache_info.raw[1]
        );
    }
    if (cpuid_info.max_xcpuid >= 0x80000006) {
        cpuid(0x80000006, 0,
            &reg[0],
            &reg[1],
            &cpuid_info.cache_info.raw[2],
            &cpuid_info.cache_info.raw[3]
        );
    }

    // Detect CPU Topology (Core/Thread) infos
    cpuid_info.topology.core_count   = -1;
    cpuid_info.topology.thread_count = -1;
    cpuid_info.topology.is_hybrid    =  0;
    cpuid_info.topology.ecore_count  = -1;
    cpuid_info.topology.pcore_count  = -1;

    int thread_per_core = 1;

    // Set correct HTT flag according to AP-485
    if (cpuid_info.max_cpuid >= 1 && cpuid_info.flags.htt) {
        cpuid(1, 0,&reg[0], &reg[1], &reg[2], &reg[3]);
        if(((reg[1] >> 16) & 0xFF) <= 1) {
            cpuid_info.flags.htt = !cpuid_info.flags.htt;
        }
    }

    switch (cpuid_info.vendor_id.str[0]) {
      case 'A':
        // AMD Processors
        if (cpuid_info.max_xcpuid >= 0x80000008) {

            cpuid(0x80000008, 0, &reg[0], &reg[1], &reg[2], &reg[3]);
            cpuid_info.topology.thread_count = (reg[2] & 0xFF) + 1;

            if (cpuid_info.max_xcpuid >= 0x8000001E) {
                cpuid(0x8000001E, 0, &reg[0], &reg[1], &reg[2], &reg[3]);

                if (((reg[1] >> 8) & 0x3) > 0) {
                    thread_per_core = 2;
                }
            } else if (cpuid_info.flags.htt) {
                if (cpuid_info.version.extendedFamily >= 8) {
                    thread_per_core = 2;
                } else {
                    cpuid_info.flags.htt = 0; // Pre-ZEN never has SMT
                }
            }
            cpuid_info.topology.core_count = cpuid_info.topology.thread_count / thread_per_core;
        }
        break;
       case 'C':
        // Cyrix / VIA / CentaurHauls / Zhaoxin
        cpuid_info.flags.htt = false;
        break;
       case 'G':
        if (cpuid_info.vendor_id.str[7] == 'T') break; // Transmeta
        // Intel
        if (cpuid_info.max_cpuid >= 0xB) {
            cpuid(0xB, 0, &reg[0], &reg[1], &reg[2], &reg[3]);
        }

        if (cpuid_info.max_cpuid >= 0xB && (reg[1] & 0xFF) != 0) { // Check if Extended Topology Information is available

            // Populate Hybrid Status (CPUID 7.EDX[15]) for Alder Lake+
            cpuid(0x7, 0, &reg[0], &reg[1], &reg[2], &reg[3]);
            if (reg[3] & (1 << 15)) {
                cpuid_info.topology.is_hybrid = 1;
                cpuid_info.topology.pcore_count  = 1; // We have at least 1 P-Core as BSP
                cpuid_info.topology.ecore_count  = 0;
            }

            for (int i = 0; i < 4; i++) {
                cpuid(0xB, i, &reg[0], &reg[1], &reg[2], &reg[3]);

                switch((reg[2] >> 8) & 0xFF) {
                    case 1: // SMT
                        thread_per_core = reg[1] & 0xFF;
                        break;
                    case 2: // Cores
                        cpuid_info.topology.thread_count = reg[1] & 0xFFFF;
                        break;
                    default:
                        continue;
                }
            }

            cpuid_info.topology.core_count = cpuid_info.topology.thread_count / thread_per_core;

        } else if (cpuid_info.max_cpuid >= 0x4) {
            cpuid(4, 0, &reg[0], &reg[1], &reg[2], &reg[3]);

            cpuid_info.topology.core_count = (reg[0] >> 26) + 1;
            cpuid_info.topology.thread_count = cpuid_info.topology.core_count;

            if (cpuid_info.flags.htt){
                if (((cpuid_info.proc_info.raw >> 16) & 0xFF) > (uint32_t)cpuid_info.topology.core_count) {
                    cpuid_info.topology.thread_count *= 2;
                } else {
                    cpuid_info.flags.htt = !cpuid_info.flags.htt;
                }
            }
        } else if (cpuid_info.max_cpuid >= 0x2) {
            if(cpuid_info.flags.htt){
                cpuid_info.topology.core_count = 1;
                cpuid_info.topology.thread_count = 2;
            }
        }
        break;
      default:
        break;
    }
}

core_type_t get_ap_hybrid_type(void)
{
    uint32_t eax, ebx, ecx, edx;

    cpuid(0x1A, 0, &eax, &ebx, &ecx, &edx);

    switch ((eax >> 24) & 0xFF) {
        case CPU_PCORE_ID:
            return CORE_PCORE;
        case CPU_ECORE_ID:
            return CORE_ECORE;
        default:
            return CORE_UNKNOWN;
    }
}
