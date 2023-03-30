// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester
#ifndef _MCH_H_
#define _MCH_H_

/* Memory configuration Detection for Intel Sandy Bridge */
void get_imc_config_intel_snb(void);

/* Memory configuration Detection for Intel Haswell */
void get_imc_config_intel_hsw(void);

/* Memory configuration Detection for Intel Skylake */
void get_imc_config_intel_skl(void);

#endif /* _MCH_H_ */