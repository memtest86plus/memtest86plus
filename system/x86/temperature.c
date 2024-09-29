// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2023 Sam Demeulemeester.

#include <stdint.h>

#include "config.h"
#include "cpuid.h"
#include "cpuinfo.h"
#include "hwquirks.h"
#include "memctrl.h"
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

static int TjMax = 0;

void get_specific_TjMax(void)
{
    // The TjMax value for some Mobile/Embedded CPUs must be read from a fixed
    // table according to their CPUID, PCI Root DID/VID or PNS.
    // Trying to read the MSR 0x1A2 on some of them trigger a reboot.

    if (cpuid_info.version.raw[0] == 0x6E8) {
        // Yonah C0 Step (Pentium/Core Duo T2000 & Celeron M 200/400)
        TjMax = 100;
    } else if (imc.family == IMC_SLT || imc.family == IMC_CLT || imc.family == IMC_TNC) {
        // Atom Silverthorne / Diamondvile
        // Atom Clover Trail/Cloverview
        // Atom Tunnel Creek / Lincroft
        TjMax = 90;
    } else if (imc.family == IMC_CDT || imc.family == IMC_PNV) {
        // Atom Silverthorne / Diamondvile
        // Atom Cedar Trail/Cedarview
        TjMax = 100;
    }
}

void temperature_init(void)
{
    uint32_t regl, regh;

    if (!enable_temperature) {
        return;
    }

    // Process temperature-related quirks
    if (quirk.type & QUIRK_TYPE_TEMP) {
        quirk.process();
    }

    // Get TjMax for Intel CPU
    if (cpuid_info.vendor_id.str[0] == 'G' && cpuid_info.max_cpuid >= 6 && (cpuid_info.dts_pmp & 1)) {

        get_specific_TjMax();

        if (TjMax == 0) {
            // Generic Method using MSR 0x1A2
            rdmsr(MSR_IA32_TEMPERATURE_TARGET, regl, regh);
            TjMax = (regl >> 16) & 0x7F;

            if (TjMax < 50 || TjMax > 125) {
                TjMax = 100;
            }
        }
    }
}

int get_cpu_temperature(void)
{
    uint32_t regl, regh;

    // Intel CPU
    if (cpuid_info.vendor_id.str[0] == 'G' && cpuid_info.max_cpuid >= 6 && (cpuid_info.dts_pmp & 1)) {

        rdmsr(MSR_IA32_THERM_STATUS, regl, regh);
        int Tabs = (regl >> 16) & 0x7F;

        return TjMax - Tabs;
    }

    // AMD CPU
    else if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.family == 0xF) { // Target only K8 & newer

        if (cpuid_info.version.extendedFamily >= 8) {        // Target Zen µarch and newer. Use SMN to get temperature.

            regl = amd_smn_read(SMN_THM_TCON_CUR_TMP);

            if ((regl >> 19) & 0x01) {
                cpu_temp_offset = -49.0f;
            }

            return cpu_temp_offset + 0.125f * (float)((regl >> 21) & 0x7FF);

        } else if (cpuid_info.version.extendedFamily == 6 && (cpuid_info.version.extendedModel == 6 || cpuid_info.version.extendedModel == 7)) { // Target family 15h (Excavator)

            pci_config_write32(0, 0, 0, AMD_SMU_INDEX_ADDR_REG, AMD_F15_M60H_TEMP_CTRL_OFFSET);
            regl = pci_config_read32(0, 0, 0, AMD_SMU_INDEX_DATA_REG);
            int raw_temp = ((regl >> 21) & 0x7FF) / 8;

            return (raw_temp > 0) ? raw_temp : 0;

        } else if (cpuid_info.version.extendedFamily > 0) { // Target K10 to K15 (Bulldozer)

            regl = pci_config_read32(0, 24, 3, AMD_TEMP_REG_K10);
            int raw_temp = ((regl >> 21) & 0x7FF) / 8;

            return (raw_temp > 0) ? raw_temp : 0;

        } else {                                            // Target K8 (CPUID ExtFamily = 0)

            regl = pci_config_read32(0, 24, 3, AMD_TEMP_REG_K8);
            int raw_temp = ((regl >> 16) & 0xFF) - 49 + cpu_temp_offset;

            return (raw_temp > 0) ? raw_temp : 0;
        }
    }

    // VIA/Centaur/Zhaoxin CPU
    else if (cpuid_info.vendor_id.str[0] == 'C' && cpuid_info.vendor_id.str[1] == 'e'
          && (cpuid_info.version.family == 6 || cpuid_info.version.family == 7)) {

        uint32_t msr_temp;

        if (cpuid_info.version.family == 7 || cpuid_info.version.model == 0xF) {
            msr_temp = MSR_VIA_TEMP_NANO;   // Zhaoxin, Nano
        } else if (cpuid_info.version.model == 0xA || cpuid_info.version.model == 0xD) {
            msr_temp = MSR_VIA_TEMP_C7;     // C7 A/D
        } else {
            return 0;
        }

        rdmsr(msr_temp, regl, regh);
        return (int)(regl & 0xffffff);
    }

    return 0;
}
