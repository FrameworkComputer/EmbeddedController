/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__
#define __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__

#include <x86_common_pwrseq.h>

#define DT_DRV_COMPAT intel_ap_pwrseq
#define INTEL_COM_POWER_NODE	DT_INST(0, intel_ap_pwrseq)

/* Check if the gpio is needed for the power sequence */
#define PWRSEQ_GPIO_PRESENT(pha) \
	DT_PHA_HAS_CELL(INTEL_COM_POWER_NODE, pha, flags)

/* Common device tree configurable attributes */
struct common_pwrseq_config {
	int pch_dsw_pwrok_delay_ms;
	int pch_pm_pwrbtn_delay_ms;
	int pch_rsmrst_delay_ms;
#if PWRSEQ_GPIO_PRESENT(en_pp5000_s5_gpios)
	const struct gpio_dt_spec enable_pp5000_a;
#endif
#if PWRSEQ_GPIO_PRESENT(en_pp3300_s5_gpios)
	const struct gpio_dt_spec enable_pp3300_a;
#endif
	const struct gpio_dt_spec pg_ec_rsmrst_odl;
	const struct gpio_dt_spec ec_pch_rsmrst_odl;
#if PWRSEQ_GPIO_PRESENT(pg_ec_dsw_pwrok_gpios)
	const struct gpio_dt_spec pg_ec_dsw_pwrok;
#endif
#if PWRSEQ_GPIO_PRESENT(ec_soc_dsw_pwrok_gpios)
	const struct gpio_dt_spec ec_soc_dsw_pwrok;
#endif
	const struct gpio_dt_spec slp_s3_l;
	const struct gpio_dt_spec all_sys_pwrgd;
	const struct gpio_dt_spec slp_sus_l;
};

enum power_states_ndsx chipset_pwr_sm_run(
				enum power_states_ndsx curr_state,
				const struct common_pwrseq_config *com_cfg);
void all_sig_pass_thru_handler(void);
void chipset_force_shutdown(enum pwrseq_chipset_shutdown_reason reason,
				const struct common_pwrseq_config *com_cfg);
void chipset_reset(enum pwrseq_chipset_shutdown_reason reason);
void common_rsmrst_pass_thru_handler(void);
void init_chipset_pwr_seq_state(void);
enum power_states_ndsx pwr_sm_get_state(void);
void apshutdown(void);

extern const char pwrsm_dbg[][25];

#endif /* __X86_NON_DSX_COMMON_H__ */
