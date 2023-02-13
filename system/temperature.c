// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2023 Sam Demeulemeester.
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
#include "hwquirks.h"
#include "msr.h"
#include "pci.h"

#include "temperature.h"

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

float cpu_temp_offset = 0;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void temperature_init(void)
{
    // Process temperature-related quirks
    if (quirk.type & QUIRK_TYPE_TEMP) {
        quirk.process();
    }
}

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
    else if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.family == 0xF) { // Target only K8 & newer

        if (cpuid_info.version.extendedFamily >= 8) {        // Target Zen µarch and newer. Use SMN to get temperature.

            uint32_t tval = amd_smn_read(SMN_THM_TCON_CUR_TMP);

            if ((tval >> 19) & 0x01) {
              cpu_temp_offset = -49.0f;
            }

            return cpu_temp_offset + 0.125f * (float)((tval >> 21) & 0x7FF);

        } else if (cpuid_info.version.extendedFamily > 0) { // Target K10 to K15 (Bulldozer)

            uint32_t rtcr = pci_config_read32(0, 24, 3, AMD_TEMP_REG_K10);
            int raw_temp = ((rtcr >> 21) & 0x7FF) / 8;

            return (raw_temp > 0) ? raw_temp : 0;

        } else {                                            // Target K8 (CPUID ExtFamily = 0)

            uint32_t rtcr = pci_config_read32(0, 24, 3, AMD_TEMP_REG_K8);
            int raw_temp = ((rtcr >> 16) & 0xFF) - 49 + cpu_temp_offset;

            return (raw_temp > 0) ? raw_temp : 0;
        }
    }

    // VIA/Centaur/Zhaoxin CPU
    else if (cpuid_info.vendor_id.str[0] == 'C' && cpuid_info.vendor_id.str[1] == 'e'
          && (cpuid_info.version.family == 6 || cpuid_info.version.family == 7)) {

        uint32_t msrl, msrh, msr_temp;

        if (cpuid_info.version.family == 7 || cpuid_info.version.model == 0xF) {
            msr_temp = MSR_VIA_TEMP_NANO;   // Zhaoxin, Nano
        } else if (cpuid_info.version.model == 0xA || cpuid_info.version.model == 0xD) {
            msr_temp = MSR_VIA_TEMP_C7;     // C7 A/D
        } else {
            return 0;
        }

        rdmsr(msr_temp, msrl, msrh);
        return (int)(msrl & 0xffffff);
    }

    return 0;
}
