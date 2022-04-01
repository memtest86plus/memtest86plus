// SPDX-License-Identifier: GPL-2.0
#ifndef CPUINFO_H
#define CPUINFO_H
/**
 * \file
 *
 * Provides information about the CPU type, clock speed and cache sizes.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 * Copyright (C) 2004-2022 Sam Demeulemeester.
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * IMC Definition
 */

#define IMC_NHM         0x1000  // Core i7 1st Gen 45 nm (Nehalem/Bloomfield)
#define IMC_WMR         0x1010  // Core 1st Gen 32 nm (Westmere)
#define IMC_SNB         0x1020  // Core 2nd Gen (Sandy Bridge)
#define IMC_IVB         0x1030  // Core 3rd Gen (Ivy Bridge)
#define IMC_HSW         0x1040  // Core 4th Gen (Haswell)
#define IMC_BDW         0x1050  // Core 5th Gen (Broadwell)
#define IMC_SKL         0x1060  // Core 6th Gen (Sky Lake-S/H/E)
#define IMC_KBL         0x1070  // Core 7/8/9th Gen (Kaby/Coffee/Comet Lake)
#define IMC_CNL         0x1080  // Cannon Lake
#define IMC_RKL         0x1090  // Core 11th Gen (Rocket Lake)
#define IMC_ADL         0x1100  // Core 12th Gen (Alder Lake-S)
#define IMC_RPL         0x1110  // Core 13th Gen (Raptor Lake)
#define IMC_MTL         0x1120  // Core 14th Gen (Meteor Lake)

#define IMC_NHM_E       0x2010  // Core i7 1st Gen 45 nm (Nehalem-E)
#define IMC_SNB_E       0x2020  // Core 2nd Gen (Sandy Bridge-E)
#define IMC_IVB_E       0x2030  // Core 3rd Gen (Ivy Bridge-E)
#define IMC_HSW_E       0x2040  // Core 3rd Gen (Haswell-E)
#define IMC_SKL_SP      0x2050  // Skylake/Cascade Lake/Cooper Lake (Server)
#define IMC_BDW_E       0x2060  // Broadwell-E (Server)
#define IMC_BDW_DE      0x2070  // Broadwell-DE (Server)
#define IMC_ICL_SP      0x2080  // Ice Lake-SP/DE (Server)
#define IMC_SPR         0x2090  // Sapphire Rapids (Server)

#define IMC_HSW_ULT     0x3010  // Core 4th Gen (Haswell-ULT)
#define IMC_SKL_UY      0x3020  // Core 6th Gen (Sky Lake-U/Y)
#define IMC_KBL_UY      0x3030  // Core 7/8/9th Gen (Kaby/Coffee/Comet/Amber Lake-U/Y)
#define IMC_ICL         0x3040  // Core 10th Gen (IceLake-Y)
#define IMC_TGL         0x3050  // Core 11th Gen (Tiger Lake-U)

#define IMC_BYT         0x4010  // Atom Bay Trail
#define IMC_CDT         0x4020  // Atom Cedar Trail

#define IMC_K8          0x8000  // Old K8
#define IMC_K10         0x8010  // K10 (Family 10h & 11h)
#define IMC_K12         0x8020  // A-Series APU (Family 12h)
#define IMC_K14         0x8030  // C- / E- / Z- Series APU (Family 14h)
#define IMC_K15         0x8040  // FX Series (Family 15h)
#define IMC_K16         0x8050  // Kabini & related (Family 16h)
#define IMC_K17         0x8060  // Zen & Zen2 (Family 17h)
#define IMC_K18         0x8070  // Hygon (Family 18h)
#define IMC_K19         0x8080  // Zen3 & Zen4(Family 19h)
#define IMC_K19_CZN     0x8081  // Cezanne APU

/**
 * A string identifying the CPU make and model.
 */
extern const char *cpu_model;

/**
 * A number identifying the integrated memory controller type.
 */
extern uint16_t imc_type;

/**
 * The size of the L1 cache in KB.
 */
extern int l1_cache;

/**
 * The size of the L2 cache in KB.
 */
extern int l2_cache;

/**
 * The size of the L3 cache in KB.
 */
extern int l3_cache;

/**
 * The bandwidth of the L1 cache
 */
extern uint32_t l1_cache_speed;

/**
 * The bandwidth of the L2 cache
 */
extern uint32_t l2_cache_speed;

/**
 * The bandwidth of the L3 cache
 */
extern uint32_t l3_cache_speed;

/**
 * The bandwidth of the RAM
 */
extern uint32_t ram_speed;

/**
 * A flag indicating that we can't read the core temperature on this CPU.
 */
extern bool no_temperature;

/**
 * The TSC clock speed in kHz. Assumed to be the nominal CPU clock speed.
 */
extern uint32_t clks_per_msec;

/**
 * Determines the CPU info and stores it in the exported variables.
 */
void cpuinfo_init(void);

/**
 * Determines the RAM & caches bandwidth and stores it in the exported variables.
 */
void membw_init(void);

#endif // CPUINFO_H
