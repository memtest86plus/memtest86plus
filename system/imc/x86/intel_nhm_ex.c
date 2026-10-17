// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Lionel Debroux
//
// ------------------------
//
// Platform-specific code for Intel Nehalem-EX (Xeon 7500) and Westmere-EX (Xeon E7) CPUs
//

#include "cpuinfo.h"
#include "cpuid.h"
#include "memctrl.h"
#include "msr.h"
#include "tsc.h"
#include "unistd.h"

#include "imc.h"

// The SMI2 memory controller configuration registers of these CPUs have no
// public register reference (they are in the NDA-only BIOS Writer's Guide,
// and unlike the non-EX Nehalem/Westmere there is no Linux EDAC driver
// providing offsets), so timings and channel population cannot be read. The
// M-Box performance monitoring MSRs (323535) are the only usable source for
// the DRAM frequency.

#define NHMEX_U_MSR_GLOBAL_CTL   0xC00   // U_MSR_PMON_GLOBAL_CTL (323535 Table 2-1)
#define NHMEX_M0_MSR_GLOBAL_CTL  0xCA0   // M0_CR_M_MSR_PERF_GLOBAL_CTL (323535 Tables 2-63, 2-64)
#define NHMEX_M0_MSR_CNT_CTL_0   0xCB0   // M0_CR_M_MSR_PMU_CNT_CTL_0 (323535 Table 2-67)
#define NHMEX_M0_MSR_CNT_0       0xCB1   // M0_CR_M_MSR_PMU_CNT_0 (323535 Table 2-68)

#define NHMEX_M_CYCLES           0x1B    // M-Box CYCLES event (323535 Table 2-84)

void get_imc_config_intel_nhm_ex(void)
{
    unsigned int model = cpuid_info.version.extendedModel << 4 | cpuid_info.version.model;
    switch (model) {
      case 0x2E:                          // Xeon 7500 Series (Nehalem-EX)
        break;
      case 0x2F:                          // Xeon E7 Family (Westmere-EX)
        break;
      default:
        return;
    }
    if (!clks_per_msec) return;

    // The uncore PMON MSRs are socket-local, so on multi-socket systems this
    // reports the memory speed of the BSP's socket. Save the PMON state that
    // is modified below, to restore it afterwards.
    uint32_t u_ctl_lo, u_ctl_hi, m_ctl_lo, m_ctl_hi, cnt_ctl_lo, cnt_ctl_hi;
    rdmsr(NHMEX_U_MSR_GLOBAL_CTL, u_ctl_lo, u_ctl_hi);
    rdmsr(NHMEX_M0_MSR_GLOBAL_CTL, m_ctl_lo, m_ctl_hi);
    rdmsr(NHMEX_M0_MSR_CNT_CTL_0, cnt_ctl_lo, cnt_ctl_hi);

    // Set up a monitoring session (323535 2.1.2): reset all counters, select
    // the M-Box CYCLES event on counter 0, then enable counting at the
    // counter, box and uncore levels. pmi_en and wrap_mode stay clear, so no
    // interrupt is generated and the 48-bit counter cannot wrap during the
    // short measurement window.
    wrmsr(NHMEX_U_MSR_GLOBAL_CTL, 1u << 29, 0);                     // rst_all pulse
    wrmsr(NHMEX_M0_MSR_CNT_CTL_0, (NHMEX_M_CYCLES << 9) | 1, 0);    // inc_sel = CYCLES, en
    wrmsr(NHMEX_M0_MSR_GLOBAL_CTL, 1, 0);                           // ctr_en[0]
    wrmsr(NHMEX_U_MSR_GLOBAL_CTL, 1u << 28, 0);                     // en_all, rst_all = 0

    // The data registers can be read without disabling event collection
    // (323535 2.7.4.2), so count M-Box clock cycles over a TSC-referenced
    // window instead of freezing and unfreezing the PMON.
    uint32_t start_lo, start_hi, end_lo, end_hi;
    rdmsr(NHMEX_M0_MSR_CNT_0, start_lo, start_hi);
    uint64_t tsc = get_tsc();
    usleep(10000);
    rdmsr(NHMEX_M0_MSR_CNT_0, end_lo, end_hi);
    tsc = get_tsc() - tsc;

    // Restore the saved PMON state, without replaying a reset command.
    wrmsr(NHMEX_U_MSR_GLOBAL_CTL, u_ctl_lo & ~(1u << 29), u_ctl_hi);
    wrmsr(NHMEX_M0_MSR_GLOBAL_CTL, m_ctl_lo, m_ctl_hi);
    wrmsr(NHMEX_M0_MSR_CNT_CTL_0, cnt_ctl_lo, cnt_ctl_hi);

    uint64_t start = (uint64_t)(start_hi & 0xFFFF) << 32 | start_lo;
    uint64_t end   = (uint64_t)(end_hi   & 0xFFFF) << 32 | end_lo;
    if (!tsc || end < start) return;

    float mhz = (float)(end - start) * clks_per_msec / (float)tsc / 1000.0f;

    // The M-Box clock domain is not specified in the public documentation.
    // The supported memory speeds are DDR3-800, DDR3-978 and DDR3-1067
    // (323341, 325120-001), i.e. 800, 977.78 and 1066.67 MT/s. Depending on
    // whether the M-Box runs at the DDR3 transfer rate, at the DDR3 clock,
    // at half of it or at twice the transfer rate, the rate in MT/s is mhz,
    // 2*mhz, 4*mhz or mhz/2. These candidate clock domains map to disjoint
    // speed sets, so the measurement can be resolved onto the supported
    // speeds unambiguously.
    static const float mts[3] = { 800.0f, 977.78f, 1066.67f };
    static const float factors[4] = { 1.0f, 2.0f, 4.0f, 0.5f };
    float freq = 0;
    for (unsigned int i = 0; i < 4 && !freq; i++) {
        float cand = mhz * factors[i];
        for (unsigned int j = 0; j < 3; j++) {
            if (cand >= mts[j] * 0.98f && cand <= mts[j] * 1.02f) {
                freq = mts[j];
                break;
            }
        }
    }
    if (!freq) return;

    // Publish only after the measurement validates. Timings and channel
    // population are not publicly readable on these IMCs.
    imc.type    = "DDR3";
    imc.width   = 0;
    imc.tCL_dec = 0;
    imc.tCL     = 0;
    imc.tRCD    = 0;
    imc.tRP     = 0;
    imc.tRAS    = 0;
    imc.freq    = freq + 0.5f;
}
