/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

static void ap_off(void)
{
	power_signal_set(PWR_PCH_PWROK, 0);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 0);
}

/* Generate SYS_PWROK->SOC if needed by system */
static void generate_pwrok_handler(void)
{
	int all_sys_pwrgd_in;

	if (power_signal_get(PWR_EC_PCH_SYS_PWROK) == 0) {
		k_msleep(AP_PWRSEQ_DT_VALUE(sys_pwrok_delay));
	}

	all_sys_pwrgd_in = power_signal_get(PWR_ALL_SYS_PWRGD);
	/* Loop through all PWROK signals defined by the board */
	if (all_sys_pwrgd_in == 0) {
		LOG_DBG("PG_EC_ALL_SYS_PWRGD deasserted, "
			"shutting AP off!");
		ap_off();
		return;
	}

	power_signal_set(PWR_EC_PCH_SYS_PWROK, all_sys_pwrgd_in);
	/* PCH_PWROK is set to combined result of ALL_SYS_PWRGD and SLP_S3 */
	power_signal_set(PWR_PCH_PWROK,
			 all_sys_pwrgd_in && !power_signal_get(PWR_SLP_S3));
}

/* Chipset specific power state machine handler */
enum power_states_ndsx chipset_pwr_sm_run(enum power_states_ndsx curr_state)
{
	/* Add chipset specific state handling if any */
	switch (curr_state) {
	case SYS_POWER_STATE_G3S5:
		board_ap_power_action_g3_s5();
		break;
	case SYS_POWER_STATE_S0S3:
		ap_off();
		break;
	case SYS_POWER_STATE_S0:
		/* Send SYS_PWROK->SoC if conditions met */
		generate_pwrok_handler();
		break;
	default:
		break;
	}
	return curr_state;
}
