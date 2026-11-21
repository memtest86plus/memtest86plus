// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester
//
// ------------------------
//
// Intel MAD address decoder (SKL to RKL, one MC, two channels). Channel and
// DIMM stages follow Linux drivers/edac/igen6_edac.c translate_to_lower_level();
// the remap inverse follows the datasheet vol. 2 REMAPBASE / REMAPLIMIT algorithm.
//

#include <stdbool.h>
#include <stdint.h>

#include "memctrl.h"

#include "imc.h"

#define ADDR_38_20(v)       ((uint64_t)(v) & 0x7FFFF00000ULL)
#define HASH_MASK(v)        ((v) & 0xFFFC0)             // Addr[19:6]
#define HASH_LSB_BIT(v)     ((((v) >> 24) & 0x7) + 6)
#define HASH_MODE(v)        (((v) >> 28) & 0x1)

#define MAD_CH_L_MAP(v)     (((v) >> 4) & 0x1)
#define MAD_DIMM_L_MAP(v)   ((v) & 0x1)

intel_mad_snap_t intel_mad;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static uint64_t ch_s_size(const intel_mad_snap_t *snap)
{
    if (snap->layout == INTEL_MAD_SKL) {
        return (uint64_t)((snap->mad_inter >> 12) & 0x7F) << 30;
    }
    return (uint64_t)((snap->mad_inter >> 12) & 0xFF) << 29;
}

// Large (l) or small DIMM of channel ch: size in bytes, device width, rank count.
static uint64_t dimm_size(const intel_mad_snap_t *snap, int ch, bool l)
{
    uint32_t v = snap->mad_dimm[ch] >> (l ? 0 : 16);
    if (snap->layout == INTEL_MAD_SKL) {
        return (uint64_t)(v & 0x3F) << 30;
    }
    return (uint64_t)(v & 0x7F) << 29;
}

static int dimm_width(const intel_mad_snap_t *snap, int ch, bool l)
{
    uint32_t v = snap->mad_dimm[ch];
    int w = l ? (v >> (snap->layout == INTEL_MAD_SKL ? 8 : 7)) & 0x3 : (v >> 24) & 0x3;
    return (w < 3) ? 8 << w : 0;
}

static int dimm_ranks(const intel_mad_snap_t *snap, int ch, bool l)
{
    uint32_t v = snap->mad_dimm[ch];
    if (snap->layout == INTEL_MAD_SKL) {
        return ((v >> (l ? 10 : 26)) & 0x1) + 1;
    }
    return ((v >> (l ? 9 : 26)) & 0x3) + 1;
}

static int parity(uint64_t v)
{
    int p = 0;
    while (v != 0) {
        p ^= 1;
        v &= v - 1;
    }
    return p;
}

// Splits addr between two slices (channels, or DIMMs of a channel). The SKL-era hash registers
// are not in the datasheet: use them only in hash mode, else interleave on Addr[6].
static int lower_level(const intel_mad_snap_t *snap, uint64_t *addr, uint32_t hash, uint64_t s_size, int l_map)
{
    if (*addr >= 2 * s_size) {
        *addr -= s_size;
        return l_map;
    }

    bool use_reg = (snap->layout != INTEL_MAD_SKL) || HASH_MODE(hash);
    int bit = use_reg ? HASH_LSB_BIT(hash) : 6;
    int id;
    if (use_reg && HASH_MODE(hash)) {
        // The interleave bit is always part of the hash.
        id = parity(*addr & (HASH_MASK(hash) | (1ULL << bit)));
    } else {
        id = (*addr >> bit) & 1;
    }
    *addr = (*addr >> (bit + 1)) << bit | (*addr & ((1ULL << bit) - 1));
    return id;
}

// System address -> memory address: the MMIO hole below 4 GB is reclaimed above it.
static bool sys_to_mem(const intel_mad_snap_t *snap, uint64_t pa, uint64_t *addr)
{
    uint64_t tolud = snap->tolud & 0xFFF00000;
    uint64_t base  = ADDR_38_20(snap->remap_base);
    uint64_t limit = ADDR_38_20(snap->remap_limit) + (1 << 20);

    if (pa < tolud) {
        *addr = pa;
    } else if (pa < 0x100000000ULL || pa >= ADDR_38_20(snap->touud)) {
        return false;
    } else if (base < limit && pa >= base && pa < limit) {
        *addr = pa - base + tolud;
    } else {
        *addr = pa;
    }
    return *addr < ADDR_38_20(snap->tom);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool intel_mad_view(const intel_mad_snap_t *snap, imc_view_t *view)
{
    // DDR4 / DDR3 only: no LPDDR, no ECHM (32-bit channel halves).
    if (snap->layout == INTEL_MAD_SKL ? (snap->mad_inter & 0x3) > 1 : (snap->mad_inter & 0xF) != 0) {
        return false;
    }

    view->n_mc     = 1;
    view->n_ch     = 2;
    view->total_mb = ADDR_38_20(snap->tom) >> 20;
    for (int ch = 0; ch < 2; ch++) {
        int l_map = MAD_DIMM_L_MAP(snap->mad_intra[ch]);
        for (int l = 0; l < 2; l++) {
            imc_dimm_view_t *dimm = &view->dimm[0][ch][l ? l_map : !l_map];
            dimm->size_mb   = dimm_size(snap, ch, l) >> 20;
            dimm->ranks     = dimm->size_mb ? dimm_ranks(snap, ch, l) : 0;
            dimm->dev_width = dimm->size_mb ? dimm_width(snap, ch, l) : 0;
        }
    }
    return true;
}

bool intel_mad_decode(const intel_mad_snap_t *snap, uint64_t pa, dram_loc_t *loc)
{
    uint64_t addr;

    if (!snap->valid || !sys_to_mem(snap, pa, &addr)) {
        return false;
    }

    int ch = lower_level(snap, &addr, snap->ch_hash, ch_s_size(snap), MAD_CH_L_MAP(snap->mad_inter));
    int l_map = MAD_DIMM_L_MAP(snap->mad_intra[ch]);
    int dimm = lower_level(snap, &addr, snap->ch_ehash, dimm_size(snap, ch, false), l_map);
    bool l = (dimm == l_map);
    if (addr >= dimm_size(snap, ch, l)) {
        return false;
    }

    loc->mc   = 0;
    loc->ch   = ch;
    loc->dimm = dimm;
    return true;
}
