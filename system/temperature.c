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

typedef enum {
    None,
    Intel,
    AmdZen,
    AmdK10,
    AmdK8,
    ViaNano,
    ViaC7
} cpu_temp_t;

static cpu_temp_t cpu_temp_type;
static cpu_temp_t get_cpu_temp_type(void);

static int TjMax = 0;

static void get_specific_TjMax(void)
{
    // The TjMax value for some Mobile/Embedded CPUs must be read from a fixed
    // table according to their CPUID, PCI Root DID/VID or PNS.
    // Trying to read the MSR 0x1A2 on some of them trigger a reboot.

    // Yonah C0 Step (Pentium/Core Duo T2000 & Celeron M 200/400)
    if (cpuid_info.version.raw[0] == 0x6E8) {
        TjMax = 100;
    }
}

void temperature_init(void)
{
    // Process temperature-related quirks
    if (quirk.type & QUIRK_TYPE_TEMP) {
        quirk.process();
    }

    cpu_temp_type = get_cpu_temp_type();
}

static cpu_temp_t get_cpu_temp_type(void)
{
    uint32_t regl, regh;

    // Intel CPU
    if (cpuid_info.vendor_id.str[0] == 'G' && cpuid_info.max_cpuid >= 6 && (cpuid_info.dts_pmp & 1)) {
        // Get TjMax for Intel CPU
        get_specific_TjMax();

        if (TjMax == 0) {
            // Generic Method using MSR 0x1A2
            rdmsr(MSR_IA32_TEMPERATURE_TARGET, regl, regh);
            TjMax = (regl >> 16) & 0x7F;

            if (TjMax < 50 || TjMax > 125) {
                TjMax = 100;
            }
        }

        return Intel;
    }

    // AMD CPU
    else if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.family == 0xF) { // Target only K8 & newer

        if (cpuid_info.version.extendedFamily >= 8) {        // Target Zen µarch and newer. Use SMN to get temperature.
            return AmdZen;
        } else if (cpuid_info.version.extendedFamily > 0) { // Target K10 to K15 (Bulldozer)
            return AmdK10;
        } else {                                            // Target K8 (CPUID ExtFamily = 0)
            return AmdK8;
        }
    }

    // VIA/Centaur/Zhaoxin CPU
    else if (cpuid_info.vendor_id.str[0] == 'C' && cpuid_info.vendor_id.str[1] == 'e'
          && (cpuid_info.version.family == 6 || cpuid_info.version.family == 7)) {

        if (cpuid_info.version.family == 7 || cpuid_info.version.model == 0xF) {
            return ViaNano;                 // Zhaoxin, Nano
        } else if (cpuid_info.version.model == 0xA || cpuid_info.version.model == 0xD) {
            return ViaC7;                   // C7 A/D
        }
    }

    return None;
}

int get_cpu_temperature(void)
{
    uint32_t regl, regh;
    int raw_temp;

    switch (cpu_temp_type) {
      case Intel:
        rdmsr(MSR_IA32_THERM_STATUS, regl, regh);
        raw_temp = (regl >> 16) & 0x7F;

        return TjMax - raw_temp;

      case AmdZen:
        regl = amd_smn_read(SMN_THM_TCON_CUR_TMP);

        if ((regl >> 19) & 0x01) {
            cpu_temp_offset = -49.0f;
        }

        return cpu_temp_offset + 0.125f * (float)((regl >> 21) & 0x7FF);

      case AmdK10:
        regl = pci_config_read32(0, 24, 3, AMD_TEMP_REG_K10);
        raw_temp = ((regl >> 21) & 0x7FF) / 8;

        return (raw_temp > 0) ? raw_temp : 0;

      case AmdK8:
        regl = pci_config_read32(0, 24, 3, AMD_TEMP_REG_K8);
        raw_temp = ((regl >> 16) & 0xFF) - 49 + cpu_temp_offset;

        return (raw_temp > 0) ? raw_temp : 0;

      case ViaNano:
        rdmsr(MSR_VIA_TEMP_NANO, regl, regh);
        return (int)(regl & 0xffffff);

      case ViaC7:
        rdmsr(MSR_VIA_TEMP_C7, regl, regh);
        return (int)(regl & 0xffffff);
    }

    return 0;
}
