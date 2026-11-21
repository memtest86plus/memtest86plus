// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2024 Sam Demeulemeester
#ifndef _IMC_H_
#define _IMC_H_

/**
 * DIMM decoder view of the memory controller: per (MC, channel, DIMM)
 * geometry, filled by the family decoder at init (values only).
 */

#define IMC_VIEW_MAX_MC     2
#define IMC_VIEW_MAX_CH     2
#define IMC_VIEW_MAX_DIMM   2

typedef struct {
    uint32_t    size_mb;    // 0 = not populated
    uint8_t     ranks;      // 0 = unknown
    uint8_t     dev_width;  // 0 = unknown
} imc_dimm_view_t;

typedef struct {
    uint8_t         n_mc;       // 0 = no decoder for this family
    uint8_t         n_ch;
    uint64_t        total_mb;   // memory behind the decoder (TOM), 0 = unknown
    imc_dimm_view_t dimm[IMC_VIEW_MAX_MC][IMC_VIEW_MAX_CH][IMC_VIEW_MAX_DIMM];
} imc_view_t;

extern imc_view_t imc_view;

/**
 * Intel MAD address decoder for the single-MC client families (SKL to RKL):
 * register snapshot taken at init, decoded by pure integer math.
 */

typedef enum {
    INTEL_MAD_SKL,          // SKL / KBL / CFL / CML: sizes x 1 GB (datasheet vol. 2 332688, 337345)
    INTEL_MAD_ICL           // RKL: sizes x 512 MB (datasheet vol. 2 636761)
} intel_mad_layout_t;

typedef struct {
    bool        valid;
    uint8_t     layout;
    uint32_t    mad_inter;
    uint32_t    mad_intra[2];
    uint32_t    mad_dimm[2];
    uint32_t    ch_hash;
    uint32_t    ch_ehash;
    uint32_t    tolud;
    uint64_t    tom;
    uint64_t    touud;
    uint64_t    remap_base;
    uint64_t    remap_limit;
} intel_mad_snap_t;

extern intel_mad_snap_t intel_mad;

/* Takes the register snapshot and fills imc_view (in intel_skl.c, shared with RKL). */
void get_mad_intel(uintptr_t mchbar_addr, intel_mad_layout_t layout);

/* Fills view from the snapshot. Returns false for unsupported configurations (view untouched). */
bool intel_mad_view(const intel_mad_snap_t *snap, imc_view_t *view);

/* Decodes physical address pa (MC, channel, DIMM). Returns false if pa is not in DRAM. */
bool intel_mad_decode(const intel_mad_snap_t *snap, uint64_t pa, dram_loc_t *loc);

/**
 * Integrated Memory Controler (IMC) Settings Detection Code
 */

/* Memory configuration Detection for 64-bit, pre-Zen AMD Family 0Fh to 16h models */
void get_imc_config_amd_prezen(void);

/* Memory configuration Detection for AMD Zen CPUs */
void get_imc_config_amd_zen(void);

/* Memory configuration Detection for Intel Nehalem / Westmere-EP */
void get_imc_config_intel_nhm(void);

/* Memory configuration Detection for Intel Westmere with graphics (Clarkdale / Arrandale / Havendale) */
void get_imc_config_intel_wmr(void);

/* Memory configuration Detection for Intel Sandy Bridge */
void get_imc_config_intel_snb(void);

/* Memory configuration Detection for Intel Haswell */
void get_imc_config_intel_hsw(void);

/* Memory configuration Detection for Intel Skylake */
void get_imc_config_intel_skl(void);

/* Memory configuration Detection for Intel Xeon Sandy Bridge-EP to Skylake-SP / Cooper Lake */
void get_imc_config_intel_ep(void);

/* Memory configuration Detection for Intel Ice Lake */
void get_imc_config_intel_icl(void);

/* Memory configuration Detection for Intel Alder Lake */
void get_imc_config_intel_adl(void);

/* Memory configuration Detection for Intel Metor Lake */
void get_imc_config_intel_mtl(void);

/* Memory configuration Detection for Loongson LoongArch DDR4 CPU family */
void get_imc_config_loongson_ddr4(void);

/**
 * ECC Polling Code for various IMCs
 */

/* ECC Polling Code for AMD Zen CPUs */
void poll_ecc_amd_zen(bool report);

#endif /* _IMC_H_ */
