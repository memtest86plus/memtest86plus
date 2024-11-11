// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.

#include <stdbool.h>
#include <stdint.h>

#include "cpuid.h"

#include "string.h"

#include <larchintrin.h>

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

cpuid_info_t cpuid_info;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void cpuid_init(void)
{
    // Get the vendor ID.
    cpuid_info.vendor_id.raw[0] = (uint32_t)(__iocsrrd_w(0x10));
    cpuid_info.vendor_id.raw[1] = (uint32_t)(__iocsrrd_w(0x14));
    cpuid_info.vendor_id.raw[2] = (uint32_t)(__iocsrrd_w(0x18));
    cpuid_info.vendor_id.str[CPUID_VENDOR_STR_LENGTH - 1] = '\0';

    cpuid_info.topology.core_count   = -1;
    cpuid_info.topology.thread_count = -1;
    cpuid_info.topology.is_hybrid    =  0;
    cpuid_info.topology.ecore_count  = -1;
    cpuid_info.topology.pcore_count  = -1;

    // Get the brand ID.
    cpuid_info.brand_id.raw[0] = (uint32_t)(__iocsrrd_w(0x10));
    cpuid_info.brand_id.raw[1] = (uint32_t)(__iocsrrd_w(0x14));
    if (__iocsrrd_w(0x18) != 0x0) {
      cpuid_info.brand_id.raw[2] = (uint32_t)(__iocsrrd_w(0x18));
    } else {
      cpuid_info.brand_id.raw[2] = 0x20202020;
    }
    if (__iocsrrd_w(0x1c) != 0x0) {
      cpuid_info.brand_id.raw[3] = (uint32_t)(__iocsrrd_w(0x1c));
    } else {
      cpuid_info.brand_id.raw[3] = 0x20202020;
    }
    cpuid_info.brand_id.raw[4] = (uint32_t)(__iocsrrd_w(0x20));
    cpuid_info.brand_id.raw[5] = (uint32_t)(__iocsrrd_w(0x24));
    if (__iocsrrd_w(0x28) != 0x0) {
      cpuid_info.brand_id.raw[6] = (uint32_t)(__iocsrrd_w(0x28));
    } else {
      cpuid_info.brand_id.raw[6] = 0x20202020;
    }
    if (__iocsrrd_w(0x2c) != 0x0) {
      cpuid_info.brand_id.raw[7] = (uint32_t)(__iocsrrd_w(0x2c));
    } else {
      cpuid_info.brand_id.raw[7] = 0x20202020;
    }
    cpuid_info.brand_id.str[CPUID_BRAND_STR_LENGTH - 1] = '\0';

    // Set correct HTT flag
    cpuid_info.flags.htt = false;
    if (strstr(cpuid_info.brand_id.str, "3A5") ||
         strstr(cpuid_info.brand_id.str, "3C5") ||
         strstr(cpuid_info.brand_id.str, "3D5") ||
         strstr(cpuid_info.brand_id.str, "3E5") ||
         strstr(cpuid_info.brand_id.str, "2K") ||
         strstr(cpuid_info.brand_id.str, "B6000M"))
    {
      cpuid_info.flags.htt = false;
    } else {
      cpuid_info.flags.htt = true;
    }
}

core_type_t get_ap_hybrid_type(void)
{
    //
    // Currently, return P-Core anyway.
    //
    return CORE_PCORE;
}
