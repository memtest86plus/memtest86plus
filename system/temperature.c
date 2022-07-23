// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
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

    // Intel CPU
    if (cpuid_info.vendor_id.str[0] == 'G' && cpuid_info.max_cpuid >= 6) {
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

    // AMD CPU
    if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.extendedFamily > 0 && cpuid_info.version.extendedFamily < 8) {

        // Untested yet
        uint32_t rtcr = pci_config_read32(0, 24, 3, 0xA4);
        int raw_temp = (rtcr >> 21) & 0x7FF;

        return raw_temp / 8;

    } else if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.extendedFamily >= 8) {

        // Grab CPU Temp. for ZEN CPUs using SNM
        uint32_t tval = amd_smn_read(SMN_THM_TCON_CUR_TMP);

        float offset = 0;

        if((tval >> 19) & 0x01) {
          offset = -49.0f;
        }

        return offset + 0.125f * (float)((tval >> 21) & 0x7FF);
    }

    return 0;
}
