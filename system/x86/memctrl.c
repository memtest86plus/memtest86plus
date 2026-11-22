// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester
//
// ------------------------
//
// Platform-specific code for IMC configuration, ECC support, etc.
//

#include <stdbool.h>

#include "config.h"
#include "cpuinfo.h"
#include "smbios.h"
#include "spd.h"

#include "memctrl.h"
#include "imc/imc.h"

#include "display.h"

imc_info_t imc = {"UNDEF", 0, 0, 0, 0, 0, 0, 0, 0};

ecc_info_t ecc_status = {false, ECC_ERR_NONE, 0, 0, 0, 0};

imc_view_t imc_view;

typedef enum {
    DECODE_UNSUPPORTED,
    DECODE_BAD_MAP,
    DECODE_BAD_TOPOLOGY,
    DECODE_DISABLED,
    DECODE_NO_SPD,
    DECODE_SPD_MISMATCH,
    DECODE_SIZE_MISMATCH,
    DECODE_OK
} decode_state_t;

// SPD slot (SMBus address - 0x50) of a DIMM position, for two channels of two DIMMs.
#define LAYOUT_CH_MAJOR     0x0     // 2 x channel + DIMM: Intel, AMD AM5
#define LAYOUT_CH_SWAP      0x1     // controller channel 0 is channel B of the board (second address of each pair)
#define LAYOUT_DIMM_MAJOR   0x2     // 2 x DIMM + channel: AMD AM4

typedef uint8_t slot_layout_t;

static decode_state_t decode_state;

static slot_layout_t slot_layout;

// SPD slot of each DIMM position, filled by the cross-check.
static uint8_t slot_map[IMC_VIEW_MAX_MC][IMC_VIEW_MAX_CH][IMC_VIEW_MAX_DIMM];

// ----------------------
// -- Private functions --
// ----------------------

static uint8_t topology_slot(slot_layout_t layout, uint8_t mc, uint8_t ch, uint8_t dimm)
{
    uint8_t chan = mc * imc_view.n_ch + ch;

    if (layout & LAYOUT_CH_SWAP) {
        chan ^= 1;
    }
    if (layout & LAYOUT_DIMM_MAJOR) {
        return dimm * imc_view.n_mc * imc_view.n_ch + chan;
    }
    return chan * IMC_VIEW_MAX_DIMM + dimm;
}

// The IMC geometry must match the SPD of the slot the layout assigns, with no SPD left over.
// any_dimm: a DIMM alone in its channel may sit in either position (boards with one DIMM per channel).
static decode_state_t check_view(slot_layout_t layout, bool any_dimm)
{
    uint32_t slot_size[MAX_SPD_SLOT] = { 0 };
    uint64_t total_mb = 0;

    for (uint8_t mc = 0; mc < imc_view.n_mc; mc++) {
        for (uint8_t ch = 0; ch < imc_view.n_ch; ch++) {
            const imc_dimm_view_t *views = imc_view.dimm[mc][ch];
            for (uint8_t dimm = 0; dimm < IMC_VIEW_MAX_DIMM; dimm++) {
                const imc_dimm_view_t *view = &views[dimm];
                if (view->size_mb == 0) {
                    continue;
                }
                uint8_t slot = topology_slot(layout, mc, ch, dimm);
                if (any_dimm && views[dimm ^ 1].size_mb == 0
                    && (slot >= MAX_SPD_SLOT || !spd_slot_cache[slot].isValid)) {
                    slot = topology_slot(layout, mc, ch, dimm ^ 1);
                }
                if (slot >= MAX_SPD_SLOT || !spd_slot_cache[slot].isValid
                    || (view->ranks != 0 && view->ranks != spd_slot_cache[slot].ranks)
                    || (view->dev_width != 0 && view->dev_width != spd_slot_cache[slot].dev_width)) {
                    return DECODE_SPD_MISMATCH;
                }
                slot_map[mc][ch][dimm] = slot;
                slot_size[slot] += view->size_mb;
                total_mb += view->size_mb;
            }
        }
    }
    for (int slot = 0; slot < MAX_SPD_SLOT; slot++) {
        uint32_t spd_size = spd_slot_cache[slot].isValid ? spd_slot_cache[slot].module_size : 0;
        if (slot_size[slot] != spd_size) {
            return DECODE_SPD_MISMATCH;
        }
    }
    if (imc_view.total_mb != 0 && total_mb != imc_view.total_mb) {
        return DECODE_SIZE_MISMATCH;
    }
    return DECODE_OK;
}

static decode_state_t decode_check(void)
{
    const spd_info *ref = NULL;
    bool amd = (amd_df.status != AMD_DF_NO_SNAPSHOT);

    if (!enable_dimm_decode) {
        return DECODE_DISABLED;
    }
    if (amd_df.status == AMD_DF_BAD_REVISION) {
        return DECODE_BAD_MAP;
    }
    if (amd_df.status == AMD_DF_BAD_TOPOLOGY) {
        return DECODE_BAD_TOPOLOGY;
    }
    if (imc_view.n_mc == 0 || imc_view.n_mc > IMC_VIEW_MAX_MC || imc_view.n_ch > IMC_VIEW_MAX_CH) {
        return DECODE_UNSUPPORTED;
    }

    for (int slot = 0; ref == NULL && slot < MAX_SPD_SLOT; slot++) {
        if (spd_slot_cache[slot].isValid) {
            ref = &spd_slot_cache[slot];
        }
    }
    if (ref == NULL || ref->dev_width == 0) {
        return DECODE_NO_SPD;
    }

    if (!amd) {
        return check_view(slot_layout, false);
    }

    // The platform layout first, then the other channel order, then the other address rule. Boards with one DIMM
    // per channel do not all follow them.
    for (int try = 0; try < 8; try++) {
        slot_layout_t layout = slot_layout ^ (try & 0x3);
        decode_state_t state = check_view(layout, try >= 4);
        if (state != DECODE_SPD_MISMATCH) {
            slot_layout = layout;
            return state;
        }
    }
    return DECODE_SPD_MISMATCH;
}

// ---------------------
// -- Public function --
// ---------------------

void memctrl_init(void)
{
    ecc_status.ecc_enabled = false;

    if (!enable_mch_read) {
        return;
    }

    switch(imc.family) {
      case IMC_K8:
      case IMC_K10:
      case IMC_K11:
      case IMC_K12:
      case IMC_K14:
      case IMC_K15:
      case IMC_K16:
        get_imc_config_amd_prezen();
        break;
      case IMC_K17:
      case IMC_K19_VRM:
      case IMC_K19_CZN:
      case IMC_K19_RPL:
      case IMC_K19_RBT:
      case IMC_K19_PHX:
      case IMC_K1A_GRG:
        get_imc_config_amd_zen();
        break;
      case IMC_NHM:
      case IMC_NHM_E:
      case IMC_WMR_E:
        get_imc_config_intel_nhm();
        break;
      case IMC_WMR:
        get_imc_config_intel_wmr();
        break;
      case IMC_SNB:
      case IMC_IVB:
        get_imc_config_intel_snb();
        break;
      case IMC_HSW:
        get_imc_config_intel_hsw();
        break;
      case IMC_SKL:
      case IMC_KBL:
        get_imc_config_intel_skl();
        break;
      case IMC_SNB_E:
      case IMC_IVB_E:
      case IMC_HSW_E:
      case IMC_BDW_E:
      case IMC_BDW_DE:
        get_imc_config_intel_ep();
        break;
      case IMC_RKL:
        get_imc_config_intel_icl();
        break;
      case IMC_RPL:
      case IMC_ADL:
        get_imc_config_intel_adl();
        break;
      case IMC_ARL:
      case IMC_MTL:
      case IMC_LNL:
      case IMC_PTL:
        get_imc_config_intel_mtl();
      default:
        break;
    }

    // Consistency check
    if (imc.tCL == 0 || imc.tRCD == 0 || imc.tRP == 0 || imc.tRAS == 0) {
        imc.freq = 0;
    }
}

void memctrl_poll_ecc(void)
{
    if (!ecc_status.ecc_enabled) {
        return;
    }

    switch(imc.family) {
      case IMC_K17:
      case IMC_K19_VRM:
      case IMC_K19_RPL:
      case IMC_K19_RBT:
      case IMC_K1A_GRG:
        poll_ecc_amd_zen(true);
        break;
      default:
        break;
    }
}

void memctrl_decode_init(void)
{
    uint8_t slot_order[MAX_SPD_SLOT];

    // AM4: SPD addresses go by DIMM position first (A1 0x50, B1 0x51, A2 0x52, B2 0x53).
    slot_layout = (amd_df.client && !amd_df.ddr5) ? LAYOUT_DIMM_MAJOR : LAYOUT_CH_MAJOR;
    if (amd_df.umc0_b) {
        slot_layout |= LAYOUT_CH_SWAP;
    }

    decode_state = decode_check();

    // Firmware lists the memory sockets by channel, then DIMM: give the SPD slots in that order.
    for (int slot = 0; slot < MAX_SPD_SLOT; slot++) {
        slot_order[slot] = slot;
    }
    if (slot_layout & LAYOUT_DIMM_MAJOR) {
        slot_order[1] = 2;
        slot_order[2] = 1;
    }
    smbios_map_spd_slots(slot_order);
}

bool memctrl_decode_addr(uint64_t pa, dram_loc_t *loc)
{
    if (decode_state != DECODE_OK) {
        return false;
    }
    if (amd_df.status == AMD_DF_OK ? !amd_df_decode(&amd_df, pa, loc) : !intel_mad_decode(&intel_mad, pa, loc)) {
        return false;
    }
    if (loc->dimm == DRAM_DIMM_ANY) {
        loc->slot      = slot_map[loc->mc][loc->ch][0];
        loc->slot_mask = 1 << loc->slot | 1 << slot_map[loc->mc][loc->ch][1];
    } else {
        loc->slot      = slot_map[loc->mc][loc->ch][loc->dimm];
        loc->slot_mask = 1 << loc->slot;
    }
    return true;
}

const char *memctrl_decode_status(void)
{
    switch (decode_state) {
      case DECODE_BAD_MAP:
        return "unsupported address map";
      case DECODE_BAD_TOPOLOGY:
        return "unsupported topology";
      case DECODE_DISABLED:
        return "disabled";
      case DECODE_NO_SPD:
        return "no SPD data";
      case DECODE_SPD_MISMATCH:
        return "IMC/SPD mismatch";
      case DECODE_SIZE_MISMATCH:
        return "size mismatch";
      case DECODE_OK:
        return "on";
      default:
        return "unsupported platform";
    }
}
