// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.
//
// Derived from an extract of memtest86+ init.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// ------------------------------------------------
// init.c - MemTest-86  Version 3.6
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdint.h>

#include "cpuid.h"
#include "cpuinfo.h"
#include "msr.h"
#include "pci.h"

#include "temperature.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int get_cpu_temperature(void)
{
    if (imc_type == 0) {
        return 0;
    }

    // Intel CPU
    if (cpuid_info.vendor_id.str[0] == 'G' && cpuid_info.max_vcpuid >= 6) {
        if (cpuid_info.dts_pmp & 1) {
            uint32_t msrl, msrh;

            rdmsr(MSR_IA32_THERM_STATUS, msrl, msrh);
            int Tabs = (msrl >> 16) & 0x7F;

            rdmsr(MSR_IA32_TEMPERATURE_TARGET, msrl, msrh);
            int Tjunc = (msrl >> 16) & 0x7F;

            if (Tjunc < 50 || Tjunc > 125) {
                Tjunc = 90;
            }
            return Tjunc - Tabs;
        }
    }

#if 0 // TODO: This doesn't give accurate results.
    // AMD CPU
    if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.extendedFamily > 0) {
        uint32_t rtcr;
        pci_conf_read(0, 24, 3, 0xA4, 4, &rtcr);
        int raw_temp = (rtcr >> 21) & 0x7FF;
        return raw_temp / 8;
    }
#endif

    return 0;
}
