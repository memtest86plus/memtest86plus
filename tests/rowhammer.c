// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// Rowhammer test (test #11). Disabled by default.
//
// Repeatedly activating ("hammering") a DRAM row leaks charge from adjacent
// rows and can flip bits in them without those rows ever being written. Modern
// (2020+) DIMMs mitigate this in-DRAM with Target Row Refresh (TRR), so the
// classic 2015 double-sided hammer no longer detects vulnerable modules. This
// test therefore uses Blacksmith-style (S&P'22) non-uniform, REFRESH-
// synchronised, many-sided patterns that try to overflow TRR's limited
// aggressor tracking.
//
// It is a stress/margining probe, not a routine data test: on real hardware it
// hammers a time-boxed sample of sites across memory and reports any induced
// flip through data_error(), but WITHOUT feeding the BadRAM accumulator
// (an adversarial hammer flip does not imply the address is defective in normal use).
// Because the physical-address -> DRAM-row mapping is not known, it first tries
// to recover the same-bank adjacent-row stride from row-buffer-conflict timing
// (DRAMA-lite, USENIX'16), falling back to a blind stride sweep when timing is
// not observable (e.g. under virtualisation).
//
// The tight activation loop and the single-access timing live in the
// architecture-specific engine (tests/x86/rowhammer_x86.c on x86-64, or the
// portable fallback below). It runs on a single core (cpu_mode ONE): a lone
// core keeps activation timing and REFRESH synchronisation clean, and since the
// DRAM flips regardless of which core hammers, running it on every core in turn
// would only multiply the runtime without adding coverage.
//
// References: Kim et al. ISCA'14; Seaborn & Dullien (google/rowhammer-test);
// Frigo et al. TRRespass S&P'20; Jattke et al. Blacksmith S&P'22 and ZenHammer
// USENIX'24; Kogler et al. Half-Double USENIX'22; Pessl et al. DRAMA USENIX'16.

#include <stdbool.h>
#include <stdint.h>

#include "cache.h"
#include "cpuid.h"
#include "cpuinfo.h"
#include "tsc.h"

#include "memctrl.h"

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

#include "rowhammer.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Replicates a byte across a full test word (e.g. 0x55 -> 0x5555...5555).
#if (ARCH_BITS == 64)
#define REP_PATTERN(b)  (UINT64_C(0x0101010101010101) * (uint8_t)(b))
#else
#define REP_PATTERN(b)  (0x01010101u * (uint8_t)(b))
#endif

// Blind same-bank row-stride guesses, tried in rotation when timing-based
// discovery is inconclusive.
static const uintptr_t rh_blind_strides[] = {
    32u << 10, 64u << 10, 128u << 10, 256u << 10, 512u << 10
};
#define RH_NUM_BLIND ((int)(sizeof(rh_blind_strides) / sizeof(rh_blind_strides[0])))

// A small set of Blacksmith-derived starting patterns. Offsets are in row-stride
// units from the site base; victims are the adjacent rows (see rh_victims()).
// The activation counts are order-of-magnitude figures from the literature and
// are refined on real hardware.
static const rh_pattern_t rh_static_patterns[] = {
    { "double-sided", 2, {0, 2},                      {1, 1},                      {0, 0},                      {1, 1},                       500000 },
    { "4-sided",      4, {0, 2, 4, 6},                {1, 1, 1, 1},                {0, 16, 32, 48},             {1, 1, 1, 1},                 800000 },
    { "8-sided",      8, {0, 2, 4, 6, 8, 10, 12, 14}, {1, 1, 1, 1, 1, 1, 1, 1},    {0, 8, 16, 24, 32, 40, 48, 56}, {1, 1, 1, 1, 1, 1, 1, 1}, 1000000 },
    { "half-double",  3, {0, 4, 2},                   {4, 4, 1},                   {0, 2, 32},                  {1, 1, 1},                    900000 },
    { "non-uniform",  4, {0, 2, 4, 6},                {4, 2, 4, 2},                {0, 8, 16, 24},              {2, 1, 2, 1},                1000000 },
};
#define RH_NUM_STATIC ((int)(sizeof(rh_static_patterns) / sizeof(rh_static_patterns[0])))

//------------------------------------------------------------------------------
// Portable hammer engine (i586 + LoongArch; x86-64 uses tests/x86/rowhammer_x86.c)
//------------------------------------------------------------------------------

#if !defined(__x86_64__)

static bool rh_precise_flag = false;

void rh_engine_init(void)
{
#if defined(__i386__)
    // Per-line CLFLUSH and the fences/timing it needs are part of the SSE2
    // baseline; without SSE2 fall back to the coarse whole-cache-flush path.
    rh_precise_flag = cpuid_info.flags.cflush && cpuid_info.flags.sse2;
#else
    rh_precise_flag = false;
#endif
}

bool rh_engine_precise(void)
{
    return rh_precise_flag;
}

void rh_hammer_seq(void *const *seq, uint32_t seq_len, uint64_t reps)
{
    if (seq_len == 0 || reps == 0) {
        return;
    }
    for (uint64_t r = 0; r < reps; r++) {
        for (uint32_t j = 0; j < seq_len; j++) {
            volatile testword_t *p = (volatile testword_t *)seq[j];
            (void)read_word(p);
#if defined(__i386__)
            if (rh_precise_flag) {
                cache_line_flush((const volatile void *)p);
            }
#endif
        }
        if (!rh_precise_flag) {
            // Coarse fallback: evict everything so the next replay re-activates.
            cache_flush();
        }
    }
    if (rh_precise_flag) {
        mem_barrier();
    }
}

uint64_t rh_probe_cycles(volatile void *addr)
{
    if (!rh_precise_flag) {
        return 0;
    }
#if defined(__i386__)
    uint64_t t0, t1;
    mem_barrier();
    load_fence();
    t0 = get_tsc();
    load_fence();
    (void)read_word((volatile testword_t *)addr);
    cache_line_flush((const volatile void *)addr);
    load_fence();
    t1 = get_tsc();
    return t1 - t0;
#else
    (void)addr;
    return 0;
#endif
}

#endif // !__x86_64__

//------------------------------------------------------------------------------
// Private helpers
//------------------------------------------------------------------------------

// Fills [addr, addr+bytes) with word, clamped to the segment and word-aligned.
static void rh_fill_region(uintptr_t addr, testword_t word, uintptr_t bytes,
                           uintptr_t seg_start, uintptr_t seg_end)
{
    uintptr_t a = round_up(addr, sizeof(testword_t));
    uintptr_t e = addr + bytes;
    if (a < seg_start) a = round_up(seg_start, sizeof(testword_t));
    if (e > seg_end)   e = seg_end;
    for (uintptr_t p = a; p + sizeof(testword_t) <= e; p += sizeof(testword_t)) {
        write_word((testword_t *)p, word);
    }
}

// Turns a pattern into an ordered aggressor address sequence for one activation
// period, realising freq/phase/amplitude by placing accesses into period slots.
// Skips any aggressor address that would fall outside the segment.
static uint32_t rh_materialize(const rh_pattern_t *pat, uintptr_t base, uintptr_t stride,
                               uintptr_t seg_start, uintptr_t seg_end,
                               void *seq[RH_MAX_PERIOD], uint32_t period)
{
    if (period > RH_MAX_PERIOD) period = RH_MAX_PERIOD;

    signed char slot[RH_MAX_PERIOD];
    for (uint32_t s = 0; s < period; s++) {
        slot[s] = -1;
    }

    for (uint8_t a = 0; a < pat->n_aggr; a++) {
        uint8_t f   = pat->freq[a] ? pat->freq[a] : 1;
        uint8_t amp = pat->amp[a]  ? pat->amp[a]  : 1;
        uint32_t step = period / f;
        if (step == 0) step = 1;
        for (uint8_t k = 0; k < f; k++) {
            uint32_t s0 = ((uint32_t)pat->phase[a] + (uint32_t)k * step) % period;
            for (uint8_t m = 0; m < amp; m++) {
                uint32_t s = (s0 + m) % period;
                if (slot[s] < 0) slot[s] = (signed char)a;
            }
        }
    }

    uint32_t len = 0;
    for (uint32_t s = 0; s < period; s++) {
        if (slot[s] < 0) continue;
        uintptr_t addr = base + (uintptr_t)pat->offset[(uint8_t)slot[s]] * stride;
        if (addr < seg_start) continue;
        if (addr + sizeof(testword_t) > seg_end) continue;
        seq[len++] = (void *)addr;
    }
    return len;
}

// Computes the victim row offsets: rows adjacent to an aggressor that are not
// themselves aggressors. Returns the count; vic_off must hold 2*RH_MAX_AGGRESSORS.
static uint32_t rh_victims(const rh_pattern_t *pat, uint8_t *vic_off)
{
    uint32_t n = 0;
    for (uint8_t a = 0; a < pat->n_aggr; a++) {
        for (int d = -1; d <= 1; d += 2) {
            int off = (int)pat->offset[a] + d;
            if (off < 0 || off > 255) continue;

            bool is_aggr = false;
            for (uint8_t b = 0; b < pat->n_aggr; b++) {
                if (pat->offset[b] == (uint8_t)off) { is_aggr = true; break; }
            }
            if (is_aggr) continue;

            bool dup = false;
            for (uint32_t k = 0; k < n; k++) {
                if (vic_off[k] == (uint8_t)off) { dup = true; break; }
            }
            if (!dup && n < 2 * RH_MAX_AGGRESSORS) {
                vic_off[n++] = (uint8_t)off;
            }
        }
    }
    return n;
}

// Fills aggressor rows with the complement of the victim word (maximum coupling)
// and victim rows with the victim word.
static void rh_fill(uintptr_t base, uintptr_t stride, const rh_pattern_t *pat,
                    const uint8_t *vic_off, uint32_t n_vic, testword_t vword,
                    uintptr_t seg_start, uintptr_t seg_end)
{
    testword_t aword = ~vword;
    for (uint8_t a = 0; a < pat->n_aggr; a++) {
        uintptr_t addr = base + (uintptr_t)pat->offset[a] * stride;
        rh_fill_region(addr, aword, RH_AGGR_BYTES, seg_start, seg_end);
    }
    for (uint32_t v = 0; v < n_vic; v++) {
        uintptr_t addr = base + (uintptr_t)vic_off[v] * stride;
        rh_fill_region(addr, vword, RH_VICTIM_BYTES, seg_start, seg_end);
    }
}

// Checks the victim rows for flips, reporting any mismatch via data_error().
static void rh_check(uintptr_t base, uintptr_t stride, const uint8_t *vic_off,
                     uint32_t n_vic, testword_t vword,
                     uintptr_t seg_start, uintptr_t seg_end)
{
    for (uint32_t v = 0; v < n_vic; v++) {
        uintptr_t addr = base + (uintptr_t)vic_off[v] * stride;
        uintptr_t a = round_up(addr, sizeof(testword_t));
        uintptr_t e = addr + RH_VICTIM_BYTES;
        if (a < seg_start) a = round_up(seg_start, sizeof(testword_t));
        if (e > seg_end)   e = seg_end;
        for (uintptr_t p = a; p + sizeof(testword_t) <= e; p += sizeof(testword_t)) {
            testword_t actual = read_word((testword_t *)p);
            if (unlikely(actual != vword)) {
                // Do NOT feed the BadRAM accumulator: an adversarial hammer flip
                // does not mean this address is defective under normal use, so it
                // must not mark the page bad. Report the error only.
                data_error((testword_t *)p, vword, actual, false);
            }
        }
    }
}

#if defined(__i386__) || defined(__x86_64__)
// Minimum time to access two addresses back-to-back (both then flushed). When
// they share a bank but not a row this incurs a row-buffer conflict and is
// measurably slower - the basis of DRAMA-style mapping recovery.
static uint64_t rh_pair_latency(uintptr_t x, uintptr_t y)
{
    const int N = 128;
    uint64_t best = ~(uint64_t)0;
    for (int i = 0; i < N; i++) {
        mem_barrier();
        load_fence();
        uint64_t t0 = get_tsc();
        load_fence();
        __asm__ __volatile__ ("movl (%0), %%eax" : : "r" (x) : "eax", "memory");
        __asm__ __volatile__ ("movl (%0), %%eax" : : "r" (y) : "eax", "memory");
        load_fence();
        uint64_t dt = get_tsc() - t0;
        cache_line_flush((const volatile void *)x);
        cache_line_flush((const volatile void *)y);
        if (dt < best) best = dt;
    }
    return best;
}

// Estimates the same-bank adjacent-row stride from row-conflict timing. Returns
// 0 (caller falls back to a blind sweep) if no stride stands out, which is the
// expected outcome under virtualisation where DRAM timing is not observable.
static uintptr_t rh_discover_stride(uintptr_t base, uintptr_t seg_end)
{
    if (!rh_engine_precise()) {
        return 0;
    }
    static const uintptr_t cand[] = {
        32u << 10, 64u << 10, 128u << 10, 256u << 10, 512u << 10, 1u << 20
    };
    uint64_t max_lat = 0, min_lat = ~(uint64_t)0;
    uintptr_t best = 0;
    for (unsigned i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        uintptr_t y = base + cand[i];
        if (y + sizeof(testword_t) > seg_end) continue;
        uint64_t lat = rh_pair_latency(base, y);
        if (lat < min_lat) min_lat = lat;
        if (lat > max_lat) { max_lat = lat; best = cand[i]; }
    }
    if (best != 0 && min_lat != ~(uint64_t)0 && max_lat > min_lat + min_lat / 3) {
        return best;
    }
    return 0;
}
#else
static uintptr_t rh_discover_stride(uintptr_t base, uintptr_t seg_end)
{
    (void)base; (void)seg_end;
    return 0;
}
#endif

// Chooses the number of activations that fit in one refresh interval (tREFI),
// the base period for the frequency-domain patterns.
static uint32_t rh_calc_period(uintptr_t probe)
{
    uint32_t trefi_ns = RH_TREFI_NS_DDR4;
    if (imc.type != NULL) {
        // A trailing '5' (DDR5 / LPDDR5) refreshes about twice as often.
        const char *t = imc.type;
        while (t[1] != '\0') t++;
        if (*t == '5') trefi_ns = RH_TREFI_NS_DDR5;
    }

    if (!rh_engine_precise() || clks_per_msec == 0) {
        return RH_DEFAULT_PERIOD;
    }

    uint64_t cyc = 0;
    const int C = 64;
    for (int i = 0; i < C; i++) {
        cyc += rh_probe_cycles((volatile void *)probe);
    }
    cyc /= C;
    if (cyc == 0) cyc = 1;

    uint64_t trefi_cyc = (uint64_t)trefi_ns * (uint64_t)clks_per_msec / 1000000ull;
    uint64_t period = trefi_cyc / cyc;
    if (period < 8)             period = 8;
    if (period > RH_MAX_PERIOD) period = RH_MAX_PERIOD;
    return (uint32_t)period;
}

// Spins until a REFRESH stall is observed (a probe far slower than typical), or
// gives up after RH_SYNC_TIMEOUT probes and hammers without synchronisation.
static void rh_sync_refresh(uintptr_t probe)
{
    if (!rh_engine_precise()) {
        return;
    }
    uint64_t typ = 0;
    const int C = 32;
    for (int i = 0; i < C; i++) {
        typ += rh_probe_cycles((volatile void *)probe);
    }
    typ /= C;
    uint64_t thresh = typ * 2 + 64;

    for (uint32_t i = 0; i < RH_SYNC_TIMEOUT; i++) {
        if (rh_probe_cycles((volatile void *)probe) > thresh) {
            return;
        }
    }
}

// Generates a randomised non-uniform pattern (the Blacksmith-style fuzzer),
// advancing the caller's reproducible PRSG state.
static void rh_fuzz_pattern(testword_t *state, rh_pattern_t *pat, uint32_t period)
{
    testword_t s = prsg(*state);

    uint8_t n = 2 + (uint8_t)(s % (RH_MAX_AGGRESSORS - 1));  // 2..RH_MAX_AGGRESSORS
    if (n > RH_MAX_AGGRESSORS) n = RH_MAX_AGGRESSORS;

    pat->name   = "fuzz";
    pat->n_aggr = n;

    uint8_t off = 0;
    for (uint8_t i = 0; i < n; i++) {
        s = prsg(s);
        off = (uint8_t)(off + 2 + (s & 1));                 // 2 or 3 rows apart
        pat->offset[i] = off;
        s = prsg(s);
        uint8_t f = 1 + (uint8_t)(s % 4);                   // frequency 1..4
        if (f > period) f = 1;
        pat->freq[i]  = f;
        s = prsg(s);
        pat->phase[i] = (uint8_t)(s % period);
        s = prsg(s);
        pat->amp[i]   = 1 + (uint8_t)(s % 2);               // amplitude 1..2
    }

    s = prsg(s);
    pat->total_acts = 400000 + (uint32_t)(s % 800000);      // 0.4M..1.2M activations
    *state = s;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_rowhammer(int my_cpu, int time_budget_secs)
{
    int ticks = 0;

    rh_engine_init();

    if (time_budget_secs < 1) {
        time_budget_secs = 1;               // pass 0 divides the table value by 3
    }

    // Total testable bytes across the segments mapped in this window.
    uintptr_t total_bytes = 0;
    for (int s = 0; s < vm_map_size; s++) {
        total_bytes += ((uintptr_t)vm_map[s].end - (uintptr_t)vm_map[s].start) + sizeof(testword_t);
    }
    if (total_bytes < sizeof(testword_t)) {
        for (int i = 0; i < RH_SITES; i++) {
            ticks++;
            if (my_cpu < 0) continue;
            do_tick(my_cpu);
            BAILOUT;
        }
        return ticks;
    }

    if (my_cpu == master_cpu) {
        display_test_stage_description("Rowhammer %s, %is/window",
            rh_engine_precise() ? "hammer" : "coarse", time_budget_secs);
    }

    // Per-window row-stride discovery and activation-period calibration, using
    // the first segment's base as the timing probe. Skipped on the dummy run.
    uintptr_t probe    = (uintptr_t)vm_map[0].start;
    uintptr_t seg0_end = (uintptr_t)vm_map[0].end + sizeof(testword_t);
    uintptr_t discovered = 0;
    uint32_t  period     = RH_DEFAULT_PERIOD;
    if (my_cpu >= 0) {
        discovered = rh_discover_stride(probe, seg0_end);
        period     = rh_calc_period(probe);
    }

#if defined(__i386__) || defined(__x86_64__)
    bool have_timer = cpuid_info.flags.rdtsc;
#else
    bool have_timer = true;                 // LoongArch get_tsc() uses rdtime.d
#endif
    uint64_t slice_cyc = 0;
    if (have_timer && clks_per_msec != 0) {
        slice_cyc = ((uint64_t)time_budget_secs * 1000ull * (uint64_t)clks_per_msec) / RH_SITES;
    }

    // Reproducible per-run PRSG seed for the fuzzer (identical every run so a
    // reported flip can be reproduced).
    testword_t fuzz_state = (testword_t)(1 + pass_num) * (testword_t)0x9e3779b1u
                          ^ ((testword_t)(my_cpu + 1) << 8);

    for (int i = 0; i < RH_SITES; i++) {
        ticks++;
        if (my_cpu < 0) {
            continue;                       // dummy sizing run: count ticks only
        }

        // Map site index i to a segment and an aligned base address.
        uintptr_t target = (total_bytes / RH_SITES) * (uintptr_t)i
                         + (total_bytes / RH_SITES) / 2;
        uintptr_t seg_start = (uintptr_t)vm_map[0].start;
        uintptr_t seg_end   = (uintptr_t)vm_map[0].end + sizeof(testword_t);
        uintptr_t site      = seg_start;
        uintptr_t acc = 0;
        for (int s = 0; s < vm_map_size; s++) {
            uintptr_t ss = (uintptr_t)vm_map[s].start;
            uintptr_t se = (uintptr_t)vm_map[s].end + sizeof(testword_t);
            uintptr_t sb = se - ss;
            if (target < acc + sb) {
                seg_start = ss;
                seg_end   = se;
                site      = ss + (target - acc);
                break;
            }
            acc += sb;
        }
        site = round_down(site, 64);
        if (site < seg_start) site = seg_start;
        test_addr[my_cpu] = site;

        bool use_fuzz = (i >= RH_SITES / 2);
        uint64_t site_start = (slice_cyc != 0) ? get_tsc() : 0;
        int iter = 0;

        do {
            uintptr_t stride = discovered ? discovered
                                          : rh_blind_strides[iter % RH_NUM_BLIND];

            rh_pattern_t fpat;
            const rh_pattern_t *pat;
            if (use_fuzz) {
                rh_fuzz_pattern(&fuzz_state, &fpat, period);
                pat = &fpat;
            } else {
                pat = &rh_static_patterns[iter % RH_NUM_STATIC];
            }

            void *seq[RH_MAX_PERIOD];
            uint32_t seq_len = rh_materialize(pat, site, stride, seg_start, seg_end, seq, period);
            if (seq_len == 0) {
                iter++;
                continue;                   // nothing fit; try another stride/pattern
            }

            uint8_t vic_off[2 * RH_MAX_AGGRESSORS];
            uint32_t n_vic = rh_victims(pat, vic_off);

            // Two data polarities (and their striped variants) catch 0->1 and
            // 1->0 flips; the aggressors carry the complement.
            static const testword_t vic_words[4] = {
                (testword_t)0,
                ~(testword_t)0,
                (testword_t)REP_PATTERN(0x55),
                (testword_t)REP_PATTERN(0xAA),
            };
            testword_t vword = vic_words[iter % 4];

            rh_fill(site, stride, pat, vic_off, n_vic, vword, seg_start, seg_end);
            cache_flush();                  // single active CPU: push fill to DRAM

            uint64_t reps = pat->total_acts / seq_len;
            if (reps == 0) reps = 1;
            if (!rh_engine_precise() && reps > RH_COARSE_MAX_REPS) {
                reps = RH_COARSE_MAX_REPS;
            }

            rh_sync_refresh(site);
            rh_hammer_seq(seq, seq_len, reps);

            rh_check(site, stride, vic_off, n_vic, vword, seg_start, seg_end);

            iter++;
        } while (!bail && slice_cyc != 0 && (get_tsc() - site_start) < slice_cyc);

        do_tick(my_cpu);
        BAILOUT;
    }

    return ticks;
}
