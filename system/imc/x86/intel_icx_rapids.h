// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Lionel Debroux
//
// ------------------------
//
// Read-only Ice Lake-SP / Sapphire Rapids / Emerald Rapids / Granite Rapids DDR4 & DDR5 frequency and population.
//


// Ice Lake-SP is DDR4 with a 0x4000 channel stride and a stepping-dependent
// DECS bus-number register (i10nm_base.c i10nm_cfg0 vs i10nm_cfg1).
// Sapphire Rapids / Emerald Rapids are DDR5 with a 0x8000 stride and fixed 0xD0.
static const struct __attribute__((packed)) {
    uint16_t decs_did;
    uint8_t busno_offset;
    uint16_t chan_stride;
    char type[5];
} icx_cfg[] = {
    [0] = { 0x3452, 0xCC, 0x4000, "DDR4" },  // ICX-SP stepping <= 3 (Tremont-D layout)
    [1] = { 0x3452, 0xD0, 0x4000, "DDR4" },  // ICX-SP stepping >= 4
    [2] = { 0x3252, 0xD0, 0x8000, "DDR5" },  // SPR-SP / EMR-SP / GNR-SP
};

static bool read_dram_clocks_mmio(uintptr_t addr, uint64_t *value)
{
    uint32_t low1, low2, high;

    for (unsigned int retry = 0; retry < 3; retry++) {
        low1 = *(volatile uint32_t *)(addr);
        high = *(volatile uint32_t *)(addr + 4);
        low2 = *(volatile uint32_t *)(addr);
        if (low1 != low2) continue;         // carry raced the high read

        if (low1 == UINT32_MAX && (high & 0xFFFF) == 0xFFFF) return false;
        *value = ((uint64_t)(high & 0xFFFF) << 32) | low1;
        return true;
    }
    return false;
}

// 48-bit freerunning DCLK counter: Linux uncore_snbep.c ICX_IMC_DCLK /
// SPR_IMC_DCLK = { 0x22b0, 0x0, 0, 1, 48 }. Freerunning: no enable write,
// counted by hardware unconditionally. DCLK cycles -> MT/s = 2 * rate.
#define RAPIDS_DCLK_FREERUNNING     0x22b0

// Returns the DDR clock in MT/s, or 0 on failure. chan_addr is the mapped
// MMIO base of a populated, enabled channel; 0x22b4 <= chan_mmio_sz is
// already guaranteed by the window size validation.
static uint32_t icx_dclk_freq(uintptr_t chan_addr)
{
    uint64_t start, end, tsc;

    if (!clks_per_msec) return 0;

    bool valid = read_dram_clocks_mmio(chan_addr + RAPIDS_DCLK_FREERUNNING, &start);
    tsc = get_tsc();
    usleep(1000);                  // ~1 ms: >= 1200 ticks at any DDR4/5 speed
    valid = read_dram_clocks_mmio(chan_addr + RAPIDS_DCLK_FREERUNNING, &end) && valid;
    tsc = get_tsc() - tsc;

    if (!valid || !tsc) return 0;
    if (end == start) return 0;                      // frozen channel
    uint64_t clocks = (end - start) & 0xFFFFFFFFFFFF; // wrap-safe, 48-bit
    // Same derivation as get_imc_freq_intel_ep(): clocks/tsc*clks_per_msec is
    // DCLK cycles per millisecond; /500 converts kHz to MT/s (x2 for DDR).
    float freq = (float)clocks * clks_per_msec / (float)tsc / 500.0f;
    return (freq >= 350 && freq <= 9000) ? freq + 0.5f : 0;
}

static bool intel_function(int bus, int dev, int func)
{
    uint32_t id = pci_config_read32(bus, dev, func, 0);
    return (id & 0xFFFF) == PCI_VID_INTEL && (id >> 16) != 0 && (id >> 16) != 0xFFFF;
}

// The DECS function maps socket-local buses; nonzero PCI segments are not
// supported by the project's legacy configuration accessors or ECAM helper.
static bool icx_socket(unsigned int *next, int cfg, int *bus0, int *bus1, uint64_t *base)
{
    while (*next < PCI_MAX_BUS * PCI_MAX_DEV * PCI_MAX_FUNC) {
        unsigned int bdf = (*next)++;
        int bus = bdf >> 8, dev = (bdf >> 3) & 31, func = bdf & 7;
        if (pci_config_read32(bus, dev, func, 0) != ((uint32_t)icx_cfg[cfg].decs_did << 16 | PCI_VID_INTEL)) continue;
        uint32_t buses = pci_config_read32(bus, dev, func, icx_cfg[cfg].busno_offset);
        if ((buses >> 16) & 0xFF) continue;
        *bus0 = buses & 0xFF;
        *bus1 = (buses >> 8) & 0xFF;
        if (!intel_function(*bus0, 0, 1)) continue;
        uint32_t bar = pci_config_read32(*bus0, 0, 1, 0xD0);
        *base = (uint64_t)(bar & 0x1FFFFFFF) << 23;
        if (*base) return true;
    }
    return false;
}

static uintptr_t icx_map(uint64_t base, uint32_t offset, uint32_t size)
{
    // Check before narrowing to uintptr_t, also on the i586 build.
    if (!size || base > UINTPTR_MAX || offset > UINTPTR_MAX - base
        || size - 1 > UINTPTR_MAX - base - offset) return 0;
    return map_region((uintptr_t)(base + offset), size, true);
}

// Returns -1 for an unreadable populated channel, 0 for absent/disabled,
// or 1 for a channel containing at least one DRAM DIMM. Never counts slots as
// channels and never counts ECC bits as data width.
static int icx_channel(uintptr_t addr, uint32_t mcmtr_offset, uint32_t mtr_offset)
{
    uint32_t mcmtr = *(volatile uint32_t *)(addr + mcmtr_offset);
    if (mcmtr == UINT32_MAX || (mcmtr & (1 << 18))) return 0;
    bool populated = false;
    for (unsigned int slot = 0; slot < 2; slot++) {
        uint32_t mtr = *(volatile uint32_t *)(addr + mtr_offset + 4 * slot);
        if (mtr == UINT32_MAX) return -1;
        if (mtr & (1 << 15)) populated = true;
    }
    return populated ? 1 : 0;
}

static /*__attribute__((noinline))*/ void get_imc_config_intel_icx(void)
{
    unsigned int model = cpuid_info.version.extendedModel << 4 | cpuid_info.version.model;
    if (model != 0x6A && model != 0x6C && model != 0x8F && model != 0xCF) return;
    // ICX-SP DECS bus-number register moved from CCh to D0h at stepping 4
    // (i10nm_cfg0 vs i10nm_cfg1). Older steppings are also the Snow Ridge /
    // Tremont-D layout; Xeon branding per model 6F is the gate, as for
    // Westmere-EX in intel_nhm.c.
    int cfg = (model == 0x6A && cpuid_info.version.stepping < 4) ? 0 :
              (model == 0x6A || model == 0x6C) ? 1 : 2;

    unsigned int next = 0;
    int bus0, bus1;
    uint64_t base;
    while (icx_socket(&next, cfg, &bus0, &bus1, &base)) {
        unsigned int channels = 0;
        bool valid = true;
        uintptr_t freqaddr = UINTPTR_MAX;
        for (int mc = 0; mc < 4; mc++) {
            if (!intel_function(bus0, 12 + mc, 0)) continue;
            uint32_t bar = pci_config_read32(bus0, 0, 1, 0xD8 + 4 * mc);
            uint32_t first = bar & 0x7FF, last = (bar >> 13) & 0x7FF;
            if (last < first) { valid = false; break; }
            uint32_t size = (last - first + 1) << 12;
            // Channel 1's MCMTR at 0x28ef8 must fit in the advertised window;
            // the channel stride is generation-dependent.
            if (size < (uint32_t)icx_cfg[cfg].chan_stride + 0x20EFC) { valid = false; break; }
            uintptr_t addr = icx_map(base, first << 12, size);
            if (!addr) { valid = false; break; }
            for (int ch = 0; ch < 2; ch++) {
                int populated = icx_channel(addr + ch * icx_cfg[cfg].chan_stride, 0x20EF8, 0x2080C);
                if (populated < 0) { valid = false; break; }
                channels += populated;
                if (populated > 0 && freqaddr == UINTPTR_MAX) { freqaddr = addr + ch * icx_cfg[cfg].chan_stride; }
            }
            if (!valid) break;
        }
        if (!valid || !channels) continue;
        // Frequency from the populated channel's freerunning DCLK counter,
        // measured, never taken from SPD/DMI.
        uint32_t freq = icx_dclk_freq(freqaddr);
        if (!freq) continue;   // unreadable/frozen: keep the SPD/DMI fallback
        // DCLK MHz -> MT/s, matching the EP readers' unit
        imc = (imc_info_t){.type = icx_cfg[cfg].type, .family = imc.family, .width = channels * 64, .freq = freq };
        return;
    }
}

static /*__attribute__((noinline))*/ void get_imc_config_intel_gnr(void)
{
    unsigned int model = cpuid_info.version.extendedModel << 4 | cpuid_info.version.model;
    if (model != 0xAD && model != 0xAE) return;

    unsigned int next = 0;
    int bus0, bus1;
    uint64_t base;
    // Granite Rapids uses the same DECS (3252h) and bus-number register (D0h)
    // as Sapphire Rapids: i10nm_base.c gnr_cfg == spr_cfg for these fields.
    // The controller arrangement below is GNR-specific and must not use the
    // SPR per-IMC BARs at D8h-E4h.
    while (icx_socket(&next, 2, &bus0, &bus1, &base)) {
        if (!intel_function(bus0, 5, 0)) continue;
        // CAPID5 at 298h needs ECAM; legacy CF8/CFC cannot read this on Intel.
        uintptr_t cfg = pci_config_mmio_addr(bus0, 5, 0);
        if (!cfg || *(volatile uint32_t *)cfg != pci_config_read32(bus0, 5, 0, 0)) continue;
        uint32_t capid5 = *(volatile uint32_t *)(cfg + 0x298);
        unsigned int expected = (capid5 >> 21) & 0xF;
        if (capid5 == UINT32_MAX || !expected || expected > 12) continue;
        uint32_t bar = pci_config_read32(bus0, 0, 1, 0xD4);
        if (bar == UINT32_MAX) continue;
        uint32_t offset = ((bar & 0x7FF) << 12) + (model == 0xAE ? 0x206000 : 0x24C000);
        unsigned int seen = 0, channels = 0;
        bool valid = true;
        // Physical PCI positions must stay distinct from compact logical IDs.
        // Fused-off gaps do not change the MMIO offset of later controllers.
        for (unsigned int physical = 0; physical < 28; physical++) {
            if (!intel_function(bus0, 5 + physical / 7, 1 + physical % 7)) continue;
            if (++seen > expected) { valid = false; break; }
            uintptr_t addr = icx_map(base, offset + physical * 0x4000, 0x4000);
            if (!addr) { valid = false; break; }
            int populated = icx_channel(addr, 0xAF8, 0xC0C);
            if (populated < 0) { valid = false; break; }
            channels += populated;
        }
        if (!valid || seen != expected || !channels) continue;
        imc = (imc_info_t){.type = "DDR5", .family = imc.family, .width = channels * 64};
        return;
    }
}
