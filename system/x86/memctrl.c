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
    DECODE_DISABLED,
    DECODE_NO_SPD,
    DECODE_ASYMMETRIC,
    DECODE_SPD_MISMATCH,
    DECODE_SIZE_MISMATCH,
    DECODE_OK
} decode_state_t;

static decode_state_t decode_state;

// ----------------------
// -- Private functions --
// ----------------------

// Default topology: SMBus address 0x50 + n = channel n / 2, DIMM n % 2.
static uint8_t topology_slot(uint8_t mc, uint8_t ch, uint8_t dimm)
{
    return (mc * imc_view.n_ch + ch) * IMC_VIEW_MAX_DIMM + dimm;
}

static decode_state_t decode_check(void)
{
    const spd_info *ref = NULL;
    uint32_t slot_size[MAX_SPD_SLOT] = { 0 };
    uint64_t total_mb = 0;

    if (!enable_dimm_decode) {
        return DECODE_DISABLED;
    }
    if (imc_view.n_mc == 0 || imc_view.n_mc > IMC_VIEW_MAX_MC || imc_view.n_ch > IMC_VIEW_MAX_CH) {
        return DECODE_UNSUPPORTED;
    }

    // Symmetric populations only.
    for (int slot = 0; slot < MAX_SPD_SLOT; slot++) {
        const spd_info *spdi = &spd_slot_cache[slot];
        if (!spdi->isValid) {
            continue;
        }
        if (ref == NULL) {
            ref = spdi;
        }
        if (spdi->module_size != ref->module_size || spdi->ranks != ref->ranks
            || spdi->dev_width != ref->dev_width || spdi->bus_width != ref->bus_width) {
            return DECODE_ASYMMETRIC;
        }
    }
    if (ref == NULL || ref->dev_width == 0) {
        return DECODE_NO_SPD;
    }

    // The IMC geometry must match the SPD of the slot the topology assigns.
    for (uint8_t mc = 0; mc < imc_view.n_mc; mc++) {
        for (uint8_t ch = 0; ch < imc_view.n_ch; ch++) {
            for (uint8_t dimm = 0; dimm < IMC_VIEW_MAX_DIMM; dimm++) {
                const imc_dimm_view_t *view = &imc_view.dimm[mc][ch][dimm];
                if (view->size_mb == 0) {
                    continue;
                }
                uint8_t slot = topology_slot(mc, ch, dimm);
                if (slot >= MAX_SPD_SLOT || !spd_slot_cache[slot].isValid
                    || (view->ranks != 0 && view->ranks != spd_slot_cache[slot].ranks)
                    || (view->dev_width != 0 && view->dev_width != spd_slot_cache[slot].dev_width)) {
                    return DECODE_SPD_MISMATCH;
                }
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
    static const uint8_t slot_order[MAX_SPD_SLOT] = { 0, 1, 2, 3, 4, 5, 6, 7 };

    smbios_map_spd_slots(slot_order);

    decode_state = decode_check();
}

bool memctrl_decode_addr(uint64_t pa, dram_loc_t *loc)
{
    if (decode_state != DECODE_OK || !intel_mad_decode(&intel_mad, pa, loc)) {
        return false;
    }
    loc->slot = topology_slot(loc->mc, loc->ch, loc->dimm);
    return loc->slot < MAX_SPD_SLOT;
}

const char *memctrl_decode_status(void)
{
    switch (decode_state) {
      case DECODE_DISABLED:
        return "disabled";
      case DECODE_NO_SPD:
        return "no SPD data";
      case DECODE_ASYMMETRIC:
        return "asymmetric DIMMs";
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
