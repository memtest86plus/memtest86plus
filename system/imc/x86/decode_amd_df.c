// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester
//
// ------------------------
//
// AMD Zen address decoder for the client platforms (AM4 / AM5):
// physical address -> memory channel (UMC) and DIMM, from a register snapshot.
// The DRAM maps and the channel hash follow Linux drivers/ras/amd/atl, run in
// the forward direction; the chip selects follow drivers/edac/amd64_edac.c.
// DF 4.2 (Phoenix) is as read on hardware: maps at D18F7x200, destination ID
// from bit 12, and one UMC for each half of a DIMM.
//

#include <stdbool.h>
#include <stdint.h>

#include "memctrl.h"

#include "imc.h"

#define ADDR_4GB            0x100000000ULL
#define MAP_ADDR_SHIFT      28          // DRAM map base / limit granularity (256 MB)

#define HASH_64K            0x1
#define HASH_2M             0x2
#define HASH_1G             0x4

typedef enum {
    MAP_OFF,                // map not valid
    MAP_UNSUPPORTED,        // too many channels, die or socket interleave
    MAP_PLAIN,              // one channel
    MAP_2CH,                // two channels interleaved on one address bit
    MAP_2CH_HASH,           // two channels, hashed interleave bit
    MAP_4CH                 // four channels interleaved on two address bits
} map_mode_t;

typedef struct {
    uint64_t    base;
    uint64_t    limit;      // inclusive
    uint16_t    dst;        // destination fabric ID (lowest channel of the map)
    uint8_t     sel;        // interleave address bit
    uint8_t     hash;       // HASH_* ranges enabled
    bool        hole;       // the legacy MMIO hole applies
} df_map_t;

amd_df_snap_t amd_df;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static unsigned int addr_bit(uint64_t addr, int bit)
{
    return (addr >> bit) & 1;
}

static map_mode_t map_fields(const amd_df_snap_t *snap, const amd_df_map_t *reg, df_map_t *map)
{
    uint32_t chans, others, hashed;

    if (snap->rev >= AMD_DF4) {
        bool df4p2  = (snap->rev == AMD_DF4P2);
        if (!(reg->ctl & 0x1)) {
            return MAP_OFF;
        }
        map->base   = (uint64_t)(reg->base & 0x0FFFFFFF) << MAP_ADDR_SHIFT;
        map->limit  = (uint64_t)(reg->limit & 0x0FFFFFFF) << MAP_ADDR_SHIFT;
        map->hole   = reg->ctl & 0x2;
        map->hash   = (reg->ctl >> 8) & 0x7;
        map->dst    = (reg->ctl >> (df4p2 ? 12 : 16)) & 0xFFF;
        map->sel    = (reg->intlv & 0x7) + 8;
        chans       = (reg->intlv >> 4) & 0x1F;
        // Dies, sockets, chip select remap.
        others      = ((reg->intlv >> 12) & 0x3) | ((reg->intlv >> 18) & 0x1) | ((reg->ctl >> 4) & 0x1);
        hashed      = df4p2 ? ~0U : 0x10;       // DF 4.2: hashed modes not seen, not decoded
    } else {
        if (!(reg->base & 0x1)) {
            return MAP_OFF;
        }
        map->base   = (uint64_t)(reg->base >> 12) << MAP_ADDR_SHIFT;
        map->limit  = (uint64_t)(reg->limit >> 12) << MAP_ADDR_SHIFT;
        map->hole   = reg->base & 0x2;
        if (snap->rev == AMD_DF2) {
            map->hash   = HASH_64K | HASH_2M | HASH_1G;
            map->dst    = reg->limit & 0xFF;
            map->sel    = ((reg->base >> 8) & 0x7) + 8;
            chans       = (reg->base >> 4) & 0xF;
            others      = ((reg->limit >> 8) & 0x1) | ((reg->limit >> 10) & 0x3);
            hashed      = 0x8;
        } else {
            bool df3p5  = (snap->rev == AMD_DF3P5);
            map->hash   = (reg->ctl >> 20) & 0x7;
            map->dst    = reg->limit & (df3p5 ? 0xFFF : 0x3FF);
            map->sel    = ((reg->base >> 9) & 0x7) + 8;
            chans       = (reg->base >> 2) & (df3p5 ? 0x1F : 0xF);
            others      = ((reg->base >> 8) & 0x1) | ((reg->base >> (df3p5 ? 7 : 6)) & (df3p5 ? 0x1 : 0x3));
            hashed      = 0xC;
        }
    }
    map->limit |= (1ULL << MAP_ADDR_SHIFT) - 1;

    if (others != 0) {
        return MAP_UNSUPPORTED;
    }
    if (chans == 0) {
        return MAP_PLAIN;
    }
    if (chans == 1) {
        return MAP_2CH;
    }
    if (chans == 3 && snap->n_ch == 4) {
        return MAP_4CH;
    }
    // The hashed modes only exist on address bit 8 (bit 9 too before DF4).
    if (chans == hashed && (map->sel == 8 || (map->sel == 9 && snap->rev < AMD_DF4))) {
        return MAP_2CH_HASH;
    }
    return MAP_UNSUPPORTED;
}

static uint64_t map_offset(const amd_df_snap_t *snap, uint32_t reg)
{
    if (!(reg & 0x1)) {
        return 0;
    }
    switch (snap->rev) {
      case AMD_DF2:
        return (uint64_t)(reg >> 20) << MAP_ADDR_SHIFT;
      case AMD_DF4:
      case AMD_DF4P2:
        return (uint64_t)(reg >> 1) << MAP_ADDR_SHIFT;
      default:
        return (uint64_t)(reg >> 12) << MAP_ADDR_SHIFT;
    }
}

// Channel selected by a hashed map: the interleave bit XOR one bit of each enabled range.
static unsigned int hashed_channel(const amd_df_snap_t *snap, const df_map_t *map, uint64_t addr)
{
    unsigned int id = addr_bit(addr, map->sel);

    switch (snap->rev) {
      case AMD_DF2:
        return id ^ addr_bit(addr, 12) ^ addr_bit(addr, 18) ^ addr_bit(addr, 21) ^ addr_bit(addr, 30);
      case AMD_DF4:
        id ^= addr_bit(addr, 14);
        id ^= (map->hash & HASH_64K) ? addr_bit(addr, 16) : 0;
        id ^= (map->hash & HASH_2M)  ? addr_bit(addr, 21) : 0;
        id ^= (map->hash & HASH_1G)  ? addr_bit(addr, 30) : 0;
        return id;
      default:
        id ^= addr_bit(addr, 14);
        id ^= (map->hash & HASH_64K) ? addr_bit(addr, 18) : 0;
        id ^= (map->hash & HASH_2M)  ? addr_bit(addr, 23) : 0;
        id ^= (map->hash & HASH_1G)  ? addr_bit(addr, 32) : 0;
        return id;
    }
}

// Number of address bits a map uses to select the channel.
static int map_bits(map_mode_t mode)
{
    return (mode == MAP_PLAIN) ? 0 : (mode == MAP_4CH) ? 2 : 1;
}

// A plain map found in several channels belongs to the one it names as its destination.
static bool plain_map_owner(const amd_df_snap_t *snap, int ch, const df_map_t *map)
{
    if (map->dst == snap->fabric_id[ch]) {
        return true;
    }
    for (int c = 0; c < snap->n_ch; c++) {
        for (int m = 0; c != ch && m < AMD_DF_MAX_MAP; m++) {
            df_map_t other;
            if (map_fields(snap, &snap->map[c][m], &other) == MAP_PLAIN
                && other.base == map->base && other.limit == map->limit) {
                return false;
            }
        }
    }
    return true;
}

// Returns true if channel ch is one of the channels of a map: the destination and the ones after it.
static bool map_member(const amd_df_snap_t *snap, int ch, const df_map_t *map, map_mode_t mode)
{
    if (mode == MAP_PLAIN) {
        return plain_map_owner(snap, ch, map);
    }
    return ((snap->fabric_id[ch] ^ map->dst) >> map_bits(mode)) == 0;
}

// Size of the DRAM behind a map, as seen by one of its channels.
static uint64_t map_channel_size(const amd_df_snap_t *snap, const df_map_t *map, map_mode_t mode)
{
    uint64_t size = map->limit + 1 - map->base;

    if (map->hole && map->base <= snap->hole_base && map->limit >= ADDR_4GB) {
        size -= ADDR_4GB - snap->hole_base;
    }
    return size >> map_bits(mode);
}

// Returns true if map m of channel ch holds pa. *na receives the normalized address.
static bool map_claims(const amd_df_snap_t *snap, int ch, int m, uint64_t pa, uint64_t *na)
{
    const amd_df_map_t *reg = &snap->map[ch][m];
    df_map_t map;

    map_mode_t mode = map_fields(snap, reg, &map);
    if (mode < MAP_PLAIN || pa < map.base || pa > map.limit || !map_member(snap, ch, &map, mode)) {
        return false;
    }

    uint64_t addr = pa;
    if (map.hole && pa >= snap->hole_base) {
        if (pa < ADDR_4GB) {
            return false;
        }
        addr -= ADDR_4GB - snap->hole_base;
    }
    addr -= map.base;

    int bits = map_bits(mode);
    if (bits != 0) {
        // DF3.5 and DF4 hash the address without base and hole, older revisions the full address.
        bool late = (snap->rev >= AMD_DF3P5);
        uint64_t sel_addr = late ? addr : pa;
        unsigned int mask = (1U << bits) - 1;
        unsigned int id = (mode == MAP_2CH_HASH) ? hashed_channel(snap, &map, sel_addr) : (sel_addr >> map.sel) & mask;
        if (id != ((unsigned int)(snap->fabric_id[ch] - map.dst) & mask)) {
            return false;
        }
        addr = (addr >> (map.sel + bits)) << map.sel | (addr & ((1ULL << map.sel) - 1));
    }

    *na = addr + ((m > 0) ? map_offset(snap, reg->offset) : 0);
    return true;
}

// Chip select registers hold address bits [39:9] in bits [31:1], bit 0 of the base is the enable.
static bool cs_match(const amd_umc_snap_t *umc, int cs, uint64_t na)
{
    uint32_t addr = (uint32_t)(na >> 8);

    if ((umc->base[cs] & 0x1) && ((addr ^ umc->base[cs]) & ~umc->mask[cs] & ~0x1U) == 0) {
        return true;
    }
    return (umc->base_sec[cs] & 0x1) && ((addr ^ umc->base_sec[cs]) & ~umc->mask_sec[cs] & ~0x1U) == 0;
}

static uint64_t cs_size(uint32_t base, uint32_t mask)
{
    uint64_t size = 1ULL << 9;

    if (!(base & 0x1)) {
        return 0;
    }
    for (mask >>= 1; mask != 0; mask &= mask - 1) {
        size <<= 1;
    }
    return size;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool amd_df_view(const amd_df_snap_t *snap, imc_view_t *view)
{
    uint64_t map_total = 0;
    uint64_t dimm_size[AMD_DF_MAX_CH][IMC_VIEW_MAX_DIMM];
    uint8_t  dimm_ranks[AMD_DF_MAX_CH][IMC_VIEW_MAX_DIMM];

    if (snap->status != AMD_DF_OK) {
        return false;
    }

    for (int ch = 0; ch < snap->n_ch; ch++) {
        for (int m = 0; m < AMD_DF_MAX_MAP; m++) {
            df_map_t map;
            map_mode_t mode = map_fields(snap, &snap->map[ch][m], &map);
            if (mode == MAP_UNSUPPORTED) {
                return false;
            }
            if (mode != MAP_OFF && map_member(snap, ch, &map, mode)) {
                map_total += map_channel_size(snap, &map, mode);
            }
        }
        const amd_umc_snap_t *umc = &snap->umc[ch];
        for (int dimm = 0; dimm < IMC_VIEW_MAX_DIMM; dimm++) {
            dimm_size[ch][dimm]  = 0;
            dimm_ranks[ch][dimm] = 0;
            for (int cs = 2 * dimm; cs < 2 * dimm + 2; cs++) {
                uint64_t size = cs_size(umc->base[cs], umc->mask[cs]) + cs_size(umc->base_sec[cs], umc->mask_sec[cs]);
                dimm_size[ch][dimm]  += size;
                dimm_ranks[ch][dimm] += (size != 0);
            }
        }
    }

    // Split channels: the two UMCs of a board channel each hold one half of its DIMMs.
    int umcs = snap->split_ch ? 2 : 1;
    for (int ch = 0; snap->split_ch && ch < snap->n_ch; ch += 2) {
        for (int dimm = 0; dimm < IMC_VIEW_MAX_DIMM; dimm++) {
            if (dimm_size[ch][dimm] != dimm_size[ch + 1][dimm] || dimm_ranks[ch][dimm] != dimm_ranks[ch + 1][dimm]) {
                return false;
            }
        }
    }

    view->n_mc     = 1;
    view->n_ch     = snap->n_ch / umcs;
    view->total_mb = map_total >> 20;
    for (int ch = 0; ch < view->n_ch; ch++) {
        for (int dimm = 0; dimm < IMC_VIEW_MAX_DIMM; dimm++) {
            view->dimm[0][ch][dimm].size_mb   = (dimm_size[ch * umcs][dimm] * umcs) >> 20;
            view->dimm[0][ch][dimm].ranks     = dimm_ranks[ch * umcs][dimm];
            view->dimm[0][ch][dimm].dev_width = 0;
        }
    }
    return true;
}

bool amd_df_decode(const amd_df_snap_t *snap, uint64_t pa, dram_loc_t *loc)
{
    int ch = -1;
    int claims = 0;
    uint64_t na = 0;

    // Each channel holds its own copy of the maps: exactly one channel must own the address.
    for (int c = 0; c < snap->n_ch; c++) {
        for (int m = 0; m < AMD_DF_MAX_MAP; m++) {
            if (map_claims(snap, c, m, pa, &na)) {
                claims++;
                ch = c;
            }
        }
    }
    if (claims != 1) {
        return false;
    }

    const amd_umc_snap_t *umc = &snap->umc[ch];
    bool dimm0 = ((umc->base[0] | umc->base[1] | umc->base_sec[0] | umc->base_sec[1]) & 0x1);
    bool dimm1 = ((umc->base[2] | umc->base[3] | umc->base_sec[2] | umc->base_sec[3]) & 0x1);
    if (!dimm0 && !dimm1) {
        return false;
    }

    loc->mc   = 0;
    loc->ch   = snap->split_ch ? ch / 2 : ch;
    loc->dimm = dimm1 ? 1 : 0;
    if (!dimm0 || !dimm1) {
        return true;
    }

    if (na >> 40) {
        return false;
    }
    for (int cs = 0; cs < AMD_UMC_MAX_CS; cs++) {
        if (!cs_match(umc, cs, na)) {
            continue;
        }
        // Chip-select hash: the DIMM select bit is XORed with the parity of the hashed address bits.
        // On DDR5 this hash and the XOR on that bit are not decoded: report the channel.
        bool hashed = (umc->hash_cs[1] & 0x1);
        if (snap->ddr5 && (hashed || (umc->addr_cfg[cs] >> 31))) {
            loc->dimm = DRAM_DIMM_ANY;
            return true;
        }
        uint64_t bits = hashed ? na & ((uint64_t)(umc->hash_cs[1] & ~0x1U) << 8) : 0;
        for (loc->dimm = cs / 2; bits != 0; bits &= bits - 1) {
            loc->dimm ^= 1;
        }
        return true;
    }
    return false;
}
