// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2024 Sam Demeulemeester
#ifndef _IMC_H_
#define _IMC_H_

/**
 * Integrated Memory Controler (IMC) Settings Detection Code
 */

#if 0
/* Memory configuration Detection for AMD Family 0Fh models */
static void get_imc_config_amd_0fh(void);
#endif
#if 0
/* Memory configuration Detection for AMD Family 10h models */
static void get_imc_config_amd_10h(void);
#endif
/* Memory configuration Detection for AMD Family 15h models 00h-0Fh */
static void get_imc_config_amd_15h(void);

/* Memory configuration Detection for AMD Zen CPUs */
static void get_imc_config_amd_zen(void);

/* Memory configuration detection for Nehalem / Westmere-EP */
static void get_imc_config_intel_nhm(void);

/* Memory configuration detection for Nehalem-EX / Westmere-EX */
static void get_imc_config_intel_nhm_ex(void);

/* Memory configuration Detection for Intel Sandy Bridge */
static void get_imc_config_intel_snb(void);

/* Memory configuration Detection for Intel Sandy Bridge-EP */
static void get_imc_config_intel_snb_ep(void);

/* DRAM clock measurement for Intel SNB/IVB/HSW/BDW EP channel PMON registers */
static uint16_t get_imc_freq_intel_ep(int bus, int dev, int func);

/* Memory configuration Detection for Intel Haswell */
static void get_imc_config_intel_hsw(void);

/* Memory configuration Detection for Intel Haswell-EP */
static void get_imc_config_intel_hsw_ep(void);

/* Memory configuration Detection for Intel Skylake */
static void get_imc_config_intel_skl(void);

/* DDR4 channel population for Skylake / Cascade Lake / Cooper Lake Xeons */
static void get_imc_config_intel_skx(void);

/* DDR5 channel population for Ice Lake / Sapphire Rapids / Emerald Rapids Xeons */
static void get_imc_config_intel_icx(void);

/* DDR5 channel population for Granite Rapids / Granite Rapids-D Xeons */
static void get_imc_config_intel_gnr(void);

/* Memory configuration Detection for Intel Ice Lake */
static void get_imc_config_intel_icl(void);

/* Memory configuration Detection for Intel Alder Lake */
static void get_imc_config_intel_adl(void);

/* Memory configuration Detection for Intel Metor Lake */
static void get_imc_config_intel_mtl(void);

/**
 * ECC Polling Code for various IMCs
 */

/* ECC Polling Code for AMD Zen CPUs */
static void poll_ecc_amd_zen(bool report);

#endif /* _IMC_H_ */
