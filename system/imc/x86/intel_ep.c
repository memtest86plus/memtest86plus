// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Lionel Debroux
// Copyright (C) 2026 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for Intel Xeon Sandy Bridge-EP to Skylake-SP / Cascade Lake / Cooper Lake
//
// References: Intel 327043, 330784, 331051; Linux sb_edac.c, skx_common.c, uncore_snbep.c.
//

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpuinfo.h"
#include "memctrl.h"
#include "pci.h"
#include "tsc.h"
#include "unistd.h"

#include "imc.h"

#define EP_MCMTR            0x7C    // bit 14: DDR4 (Haswell-EP / Broadwell-EP)
#define EP_DIMMMTR          0x80    // one register per DIMM slot

// Channel PMON function, which also holds the timings.
#define EP_PMON_FIXED_CTR   0xD0    // 48-bit DCLK counter
#define EP_PMON_FIXED_CTL   0xF0
#define EP_PMON_BOX_CTL     0xF4
#define EP_TIMING           0x200   // tRCD[4:0], tCL[13:9], tRAS[24:19], tRP[8:5] (DDR3) / [31:27] (DDR4)

enum { EP_DDR3, EP_DDR3_DDR4, EP_DDR4 };

typedef struct __attribute__((packed)) {
    uint8_t  mtr_dev;       // function holding the DIMMMTR registers
    uint8_t  mtr_func;
    uint16_t did;           // its device ID
    uint8_t  tmg_dev;       // PMON / timing function
    uint8_t  tmg_func;
} ep_channel_t;

typedef struct __attribute__((packed)) {
    uint16_t     family;    // IMC_*
    uint8_t      type;
    uint8_t      slots;     // DIMMMTR registers per channel
    uint8_t      pop_bit;   // DIMMMTR DIMM_POP bit
    uint8_t      num_channels;
    ep_channel_t ch[8];     // channels of both memory controllers; absent ones fail the device ID check
} ep_imc_t;

static const ep_imc_t ep_imcs[] = {
    { IMC_SNB_E, EP_DDR3, 3, 14, 4, {
        { 15, 2, 0x3CAA, 16, 4 }, { 15, 3, 0x3CAB, 16, 5 }, { 15, 4, 0x3CAC, 16, 0 }, { 15, 5, 0x3CAD, 16, 1 },
    } },
    { IMC_IVB_E, EP_DDR3, 3, 14, 8, {
        { 15, 2, 0x0EAA, 16, 4 }, { 15, 3, 0x0EAB, 16, 5 }, { 15, 4, 0x0EAC, 16, 0 }, { 15, 5, 0x0EAD, 16, 1 },
        { 29, 2, 0x0E6A, 30, 4 }, { 29, 3, 0x0E6B, 30, 5 }, { 29, 4, 0x0E6C, 30, 0 }, { 29, 5, 0x0E6D, 30, 1 },
    } },
    { IMC_HSW_E, EP_DDR3_DDR4, 3, 14, 8, {
        { 19, 2, 0x2FAA, 20, 0 }, { 19, 3, 0x2FAB, 20, 1 }, { 19, 4, 0x2FAC, 21, 0 }, { 19, 5, 0x2FAD, 21, 1 },
        { 22, 2, 0x2F6A, 23, 0 }, { 22, 3, 0x2F6B, 23, 1 }, { 22, 4, 0x2F6C, 24, 0 }, { 22, 5, 0x2F6D, 24, 1 },
    } },
    { IMC_BDW_E, EP_DDR3_DDR4, 3, 14, 8, {
        { 19, 2, 0x6FAA, 20, 0 }, { 19, 3, 0x6FAB, 20, 1 }, { 19, 4, 0x6FAC, 21, 0 }, { 19, 5, 0x6FAD, 21, 1 },
        { 22, 2, 0x6F6A, 23, 0 }, { 22, 3, 0x6F6B, 23, 1 }, { 22, 4, 0x6F6C, 24, 0 }, { 22, 5, 0x6F6D, 24, 1 },
    } },
    { IMC_BDW_DE, EP_DDR3_DDR4, 3, 14, 4, {
        { 19, 2, 0x6FAA, 20, 0 }, { 19, 3, 0x6FAB, 20, 1 }, { 19, 4, 0x6FAC, 21, 0 }, { 19, 5, 0x6FAD, 21, 1 },
    } },
    { IMC_SKL_SP, EP_DDR4, 2, 15, 6, {
        { 10, 0, 0x2040, 10, 2 }, { 10, 4, 0x2044, 10, 6 }, { 11, 0, 0x2048, 11, 2 },
        { 12, 0, 0x2040, 12, 2 }, { 12, 4, 0x2044, 12, 6 }, { 13, 0, 0x2048, 13, 2 },
    } },
};

// Reads the 48-bit DCLK counter, retrying on a carry between the two halves.
static bool read_dclk_counter(int bus, int dev, int func, uint64_t *clocks)
{
    for (int i = 0; i < 3; i++) {
        uint32_t high = pci_config_read32(bus, dev, func, EP_PMON_FIXED_CTR + 4);
        uint32_t low  = pci_config_read32(bus, dev, func, EP_PMON_FIXED_CTR);
        if (pci_config_read32(bus, dev, func, EP_PMON_FIXED_CTR + 4) == high) {
            *clocks = ((uint64_t)(high & 0xFFFF) << 32) | low;
            return true;
        }
    }
    return false;
}

// Measures the data rate (MT/s) over 10 ms with the DCLK counter; 0 on failure.
static uint16_t measure_freq(int bus, int dev, int func)
{
    uint32_t ctl = pci_config_read32(bus, dev, func, EP_PMON_FIXED_CTL);
    uint32_t box = pci_config_read32(bus, dev, func, EP_PMON_BOX_CTL);
    if (!clks_per_msec || (box & 3)) return 0;

    // Enable the counter (bit 22) without reset; box control is 0 on SNB-EP, bits 17:16 set on later parts.
    pci_config_write32(bus, dev, func, EP_PMON_FIXED_CTL, 1 << 22);
    pci_config_write32(bus, dev, func, EP_PMON_BOX_CTL, imc.family == IMC_SNB_E ? 0 : 3 << 16);
    uint64_t start, end;
    bool valid = read_dclk_counter(bus, dev, func, &start);
    uint64_t tsc = get_tsc();
    usleep(10000);
    valid = read_dclk_counter(bus, dev, func, &end) && valid;
    tsc = get_tsc() - tsc;
    // Restore the controls, without replaying the counter reset (bit 19).
    pci_config_write32(bus, dev, func, EP_PMON_FIXED_CTL, ctl & ~(1 << 19));
    pci_config_write32(bus, dev, func, EP_PMON_BOX_CTL, box);

    if (!valid || !tsc) return 0;
    uint64_t clocks = (end - start) & 0xFFFFFFFFFFFF;
    float freq = (float)clocks * clks_per_msec / (float)tsc / 500.0f;
    return (freq >= 350 && freq <= 5000) ? freq + 0.5f : 0;
}

void get_imc_config_intel_ep(void)
{
    const ep_imc_t *ep = NULL;
    for (unsigned int i = 0; i < sizeof(ep_imcs) / sizeof(ep_imcs[0]); i++) {
        if (ep_imcs[i].family == imc.family) {
            ep = &ep_imcs[i];
            break;
        }
    }
    if (!ep) return;

    // Use the first socket with populated channels; the timings come from its first populated channel.
    for (int bus = 0; bus < PCI_MAX_BUS; bus++) {
        const ep_channel_t *first = NULL;
        int channels = 0;
        for (int i = 0; i < ep->num_channels; i++) {
            const ep_channel_t *ch = &ep->ch[i];
            if (pci_config_read32(bus, ch->mtr_dev, ch->mtr_func, 0) != ((uint32_t)ch->did << 16 | PCI_VID_INTEL)) {
                continue;
            }
            for (int slot = 0; slot < ep->slots; slot++) {
                uint32_t mtr = pci_config_read32(bus, ch->mtr_dev, ch->mtr_func, EP_DIMMMTR + 4 * slot);
                // A DIMM counts when one of its ranks (count in [13:12]) is not disabled in RANK_DISABLE [19:16].
                if ((mtr & (1 << ep->pop_bit)) && (~(mtr >> 16) & ((1 << (1 << ((mtr >> 12) & 3))) - 1) & 0xF)) {
                    if (!first) first = ch;
                    channels++;
                    break;
                }
            }
        }
        if (!channels) continue;

        uint32_t t = pci_config_read32_ecam(bus, first->tmg_dev, first->tmg_func, EP_TIMING);
        if (t == 0xFFFFFFFF) return;
        uint16_t freq = measure_freq(bus, first->tmg_dev, first->tmg_func);
        if (!freq) return;

        bool ddr4 = ep->type == EP_DDR4
                 || (ep->type == EP_DDR3_DDR4 && (pci_config_read32(bus, first->mtr_dev, 0, EP_MCMTR) & (1 << 14)));
        imc.type    = ddr4 ? "DDR4" : "DDR3";
        imc.width   = channels * 64;
        imc.tCL_dec = 0;
        imc.tCL     = (t >> 9) & 0x1F;
        imc.tRCD    = t & 0x1F;
        imc.tRP     = ddr4 ? (t >> 27) & 0x1F : (t >> 5) & 0xF;
        imc.tRAS    = (t >> 19) & 0x3F;
        imc.freq    = freq;
        return;
    }
}
