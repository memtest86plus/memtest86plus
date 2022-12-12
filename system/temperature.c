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

static uint16_t pci_read_pid(int bus, int dev, int func)
{
    return pci_config_read16(bus, dev, func, 2);
}

static uint16_t pci_read_vid(int bus, int dev, int func)
{
    return pci_config_read16(bus, dev, func, 0);
}

static int k8dev, k8fun;

// find the first PCI device with the related IDs
// On a multi-socketed K8, it grab only the first one since memtest only show
// one temp
static bool find_k8_temp(void)
{
    uint32_t pid, vid;

    for (k8dev = 0; k8dev < 32; k8dev++) {
        for (k8fun = 0; k8fun < 8; k8fun++) {
            vid = pci_read_vid(0, k8dev, k8fun);
            pid = pci_read_pid(0, k8dev, k8fun);
            if (vid == PCI_VID_AMD && pid == 0x1103)
                return true;
        }
    }
    return false;
}

static int k8_temp(void)
{
    // k8temp is stored on a PCI device ([AMD] K8 [Athlon64/Opteron] Miscellaneous Control)
#define K8_REG_TEMP   0xe4
#define K8_SEL_CORE   0x04
    uint32_t reg;
    int temp = 0;

    if (!find_k8_temp())
       return 0;

    if (cpuid_info.version.model == 4 && cpuid_info.version.stepping == 0)
        return 0;
    if (cpuid_info.version.model == 5 && cpuid_info.version.stepping <= 1)
        return 0;
    // TODO: backport Linux test is_rev_g_desktop() which add 21°

    // We want temperature of core0
    if (cpuid_info.version.model >= 0x40)
        temp = K8_SEL_CORE;
    pci_config_write8(0, k8dev, k8fun, K8_REG_TEMP, temp);
    reg = pci_config_read32(0, k8dev, k8fun, K8_REG_TEMP);
    // formula is taken from k8temp Linux driver
    temp = (reg >> 16) & 0xFF;

    // nobody will use memtest in a frozen env
    if (temp < 49)
        return 0;

    return temp - 49;
}

// K10 temperature is stored in a PCI device [AMD] Family 10h Processor Miscellaneous Control
// There is one PCI device per CPU socket
static int k10dev, k10fun;

#define CPUID_PKGTYPE_MASK      0xF0000000
#define CPUID_PKGTYPE_F         0x00000000
#define CPUID_PKGTYPE_AM2R2_AM3 0x10000000
#define REG_DCT0_CONFIG_HIGH    0x094
#define DDR3_MODE 1 << 8
static bool has_erratum_319(void)
{
    uint32_t eax, ebx, ecx, edx;
    uint32_t pkg_type, reg_dram_cfg;

    if (cpuid_info.version.family != 0x10)
        return false;

    cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);

    pkg_type = ebx & CPUID_PKGTYPE_MASK;

    if (pkg_type == CPUID_PKGTYPE_F)
        return true;
    if (pkg_type != CPUID_PKGTYPE_AM2R2_AM3)
        return false;

    reg_dram_cfg = pci_config_read32(0, k10dev, 2, REG_DCT0_CONFIG_HIGH);
    if (reg_dram_cfg & DDR3_MODE)
        return false;

    return false;
}

static const uint32_t k10temp_id_table[] = {
    0x1203,
};
static bool find_k10_temp(void)
{
    uint32_t pid, vid, i;

    for (k10dev = 0; k10dev < 32; k10dev++) {
        for (k10fun = 0; k10fun < 8; k10fun++) {
            vid = pci_read_vid(0, k10dev, k10fun);
            pid = pci_read_pid(0, k10dev, k10fun);
            if (vid != PCI_VID_AMD)
               continue;
            for (i = 0; i < sizeof(k10temp_id_table); i++)
                if (pid == k10temp_id_table[i])
                    return true;
        }
    }
    return false;
}

static int k10_temp(void)
{
#define K10_REG_TEMP   0xa4
    uint32_t reg;
    int temp;

    if (!find_k10_temp())
       return 0;

    if (has_erratum_319())
        return 0;

    reg = pci_config_read32(0, k10dev, k10fun, K10_REG_TEMP);
    // formula is taken from k10temp Linux driver
    temp = (reg >> 21) / 8;

    return temp;
}

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
    if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.extendedFamily == 0) {
        return k8_temp();
    }
    if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.extendedFamily > 0 && cpuid_info.version.extendedFamily < 8) {
        return k10_temp();
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
