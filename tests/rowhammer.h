// SPDX-License-Identifier: GPL-2.0
#ifndef ROWHAMMER_H
#define ROWHAMMER_H
/**
 * \file
 *
 * Internal API and tunables for the rowhammer test (test #11).
 *
 * The test hammers DRAM rows with Blacksmith-style non-uniform, REFRESH-
 * synchronised access patterns to try to induce disturbance bit flips in
 * neighbouring (victim) rows. It is disabled by default and intended as a
 * stress/margining probe, not a routine data test.
 *
 * The architecture-specific hammer engine (the tight activation loop and the
 * single-access timing used for row discovery and REFRESH synchronisation) is
 * provided by tests/x86/rowhammer_x86.c on x86-64 and by a portable fallback in
 * tests/rowhammer.c on every other target. The orchestration (site sampling,
 * DRAM row-stride discovery, pattern generation, victim fill/check and error
 * reporting) lives in tests/rowhammer.c and is architecture independent.
 *
 *//*
 * Copyright (C) 2004-2026 Sam Demeulemeester.
 */

#include <stdbool.h>
#include <stdint.h>

//------------------------------------------------------------------------------
// Tunable parameters
//------------------------------------------------------------------------------

/** Number of sample sites hammered per mapped window. Also the progress-tick
 *  count returned per window, so it must be a compile-time constant (the dummy
 *  sizing run and the real run must agree). */
#define RH_SITES                64

/** Maximum number of distinct aggressor rows in a pattern (n-sided upper bound). */
#define RH_MAX_AGGRESSORS       16

/** Maximum materialised aggressor-sequence length for one refresh interval
 *  (bounds the on-stack slot/sequence arrays). */
#define RH_MAX_PERIOD           128

/** Fallback activation-sequence period when timing calibration is unavailable. */
#define RH_DEFAULT_PERIOD       64

/** Nominal refresh interval (tREFI) used to size the per-interval activation
 *  budget, in nanoseconds. DDR5 refreshes roughly twice as often as DDR4. */
#define RH_TREFI_NS_DDR4        7800
#define RH_TREFI_NS_DDR5        3900

/** Upper bound on probes spent hunting for a REFRESH stall before hammering
 *  without synchronisation (keeps the test from spinning forever where DRAM
 *  timing is not observable, e.g. under virtualisation). */
#define RH_SYNC_TIMEOUT         4096

/** Blind row-stride guesses tried when timing-based discovery is inconclusive. */
#define RH_DEFAULT_STRIDE       (128u << 10)

/** Bytes filled/checked per victim row (a page around the aggressor-aligned
 *  offset) and per aggressor row (one line is enough to activate the row). */
#define RH_VICTIM_BYTES         4096
#define RH_AGGR_BYTES           64

/** Coarse (whole-cache-flush) fallback engine: cap on sequence replays so a
 *  wbinvd-per-replay path still completes in reasonable time. */
#define RH_COARSE_MAX_REPS      64

//------------------------------------------------------------------------------
// Pattern description
//------------------------------------------------------------------------------

/**
 * A hammer pattern in Blacksmith's frequency/phase/amplitude form. Aggressor
 * rows are placed at row offsets from the site base (in units of the discovered
 * or blindly-guessed row stride). Within one activation "period" each aggressor
 * appears `freq` times, starting at slot `phase`, in bursts of `amp`
 * consecutive accesses. The materialiser (rh_materialize) turns this into an
 * ordered address sequence; the engine replays that sequence until `total_acts`
 * activations have been issued. Victim rows are inferred as the rows adjacent to
 * the aggressors that are not themselves aggressors.
 */
typedef struct {
    const char *name;
    uint8_t     n_aggr;
    uint8_t     offset[RH_MAX_AGGRESSORS];   // row offsets (in stride units)
    uint8_t     freq  [RH_MAX_AGGRESSORS];   // appearances per period
    uint8_t     phase [RH_MAX_AGGRESSORS];   // start slot within the period
    uint8_t     amp   [RH_MAX_AGGRESSORS];   // consecutive accesses per appearance
    uint32_t    total_acts;                  // activations to issue per hammer
} rh_pattern_t;

//------------------------------------------------------------------------------
// Architecture-specific hammer engine (see file header)
//------------------------------------------------------------------------------

/**
 * One-time detection of engine capabilities (CLFLUSHOPT support, whether a
 * per-line-flush "precise" engine with usable timing is available). Idempotent.
 */
void rh_engine_init(void);

/**
 * Returns true if the precise engine (per-line cache-line flush plus usable
 * single-access timing) is available. When false, only the coarse
 * whole-cache-flush hammer path runs and row discovery / REFRESH sync are
 * skipped.
 */
bool rh_engine_precise(void);

/**
 * Replays the aggressor address sequence `reps` times. Each array element is
 * accessed once (a cached read followed by a cache-line flush), producing one
 * DRAM row activation. Ends with a memory barrier so every access has drained.
 */
void rh_hammer_seq(void *const *seq, uint32_t seq_len, uint64_t reps);

/**
 * Times a single uncached read of `addr` (read, then flush the line), returning
 * the elapsed TSC cycles. Used both to detect same-bank row-buffer conflicts
 * (DRAMA-lite discovery) and to spot REFRESH stalls. Returns 0 when precise
 * timing is unavailable.
 */
uint64_t rh_probe_cycles(volatile void *addr);

#endif // ROWHAMMER_H
