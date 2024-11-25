// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2024 Sam Demeulemeester
#ifndef _IMC_H_
#define _IMC_H_

/**
 * Integrated Memory Controler (IMC) Settings Detection Code
 */

/* Memory configuration Detection for AMD Zen CPUs */
void get_imc_config_amd_zen(void);

/* Memory configuration Detection for Intel Sandy Bridge */
void get_imc_config_intel_snb(void);

/* Memory configuration Detection for Intel Haswell */
void get_imc_config_intel_hsw(void);

/* Memory configuration Detection for Intel Skylake */
void get_imc_config_intel_skl(void);

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
