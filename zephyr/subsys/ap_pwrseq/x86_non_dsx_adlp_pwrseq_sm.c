/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_adlp_pwrseq_sm.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

static const struct chipset_pwrseq_config chip_cfg = {
	.pch_pwrok_delay_ms = DT_INST_PROP(0, pch_pwrok_delay),
	.sys_pwrok_delay_ms = DT_INST_PROP(0, sys_pwrok_delay),
	.vccst_pwrgd_delay_ms = DT_INST_PROP(0, vccst_pwrgd_delay),
	.vrrdy_timeout_ms = DT_INST_PROP(0, vrrdy_timeout),
	.sys_reset_delay_ms = DT_INST_PROP(0, sys_reset_delay),
	.all_sys_pwrgd_timeout = DT_INST_PROP(0, all_sys_pwrgd_timeout),
};

void ap_off(void)
{
	power_signal_set(PWR_VCCST_PWRGD, 0);
	power_signal_set(PWR_PCH_PWROK, 0);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 0);
}

/* Handle ALL_SYS_PWRGD signal
 * This will be overridden if the custom signal handler is needed
 */
int all_sys_pwrgd_handler(void)
{
	int retry = 0;

	/* TODO: Add condition for no power sequencer */
	k_msleep(chip_cfg.all_sys_pwrgd_timeout);

	if (power_signal_get(PWR_DSW_PWROK) == 0) {
	/* Todo: Remove workaround for the retry
	 * without this change the system hits G3 as it detects
	 * ALL_SYS_PWRGD as 0 and then 1 as a glitch
	 */
		while (power_signal_get(PWR_ALL_SYS_PWRGD) == 0) {
			if (++retry > 2) {
				LOG_ERR("PG_EC_ALL_SYS_PWRGD not ok");
				ap_off();
				return -1;
			}
			k_msleep(10);
		}
	}

	/* PG_EC_ALL_SYS_PWRGD is asserted, enable VCCST_PWRGD_OD. */

	if (power_signal_get(PWR_VCCST_PWRGD) == 0) {
		k_msleep(chip_cfg.vccst_pwrgd_delay_ms);
		power_signal_set(PWR_VCCST_PWRGD, 1);
	}
	return 0;
}

/*
 * We have asserted VCCST_PWRGO_OD, now wait for the IMVP9.1
 * to assert IMVP9_VRRDY_OD.
 *
 * Returns state of VRRDY.
 */

static int wait_for_vrrdy(void)
{
	int timeout_ms = chip_cfg.vrrdy_timeout_ms;

	for (; timeout_ms > 0; --timeout_ms) {
		if (power_signal_get(PWR_IMVP9_VRRDY) != 0)
			return 1;
		k_msleep(1);
	}
	return 0;
}

/* PCH_PWROK to PCH from EC */
__attribute__((weak)) int generate_pch_pwrok_handler(int delay)
{
	/* Enable PCH_PWROK, gated by VRRDY. */
	if (power_signal_get(PWR_PCH_PWROK) == 0) {
		if (wait_for_vrrdy() == 0) {
			LOG_DBG("Timed out waiting for VRRDY, "
				"shutting AP off!");
			ap_off();
			return -1;
		}
		k_msleep(delay);
		power_signal_set(PWR_PCH_PWROK, 1);
		LOG_DBG("Turning on PCH_PWROK");
	}

	return 0;
}

/* Generate SYS_PWROK->SOC if needed by system */
void generate_sys_pwrok_handler(const struct common_pwrseq_config *com_cfg)
{
	/* Enable PCH_SYS_PWROK. */
	if (power_signal_get(PWR_EC_PCH_SYS_PWROK) == 0) {
		k_msleep(chip_cfg.sys_pwrok_delay_ms);
		/* Check if we lost power while waiting. */
		if (power_signal_get(PWR_ALL_SYS_PWRGD) == 0) {
			LOG_DBG("PG_EC_ALL_SYS_PWRGD deasserted, "
				"shutting AP off!");
			ap_off();
			return;
		}
		LOG_INF("Turning on PWR_EC_PCH_SYS_PWROK");
		power_signal_set(PWR_EC_PCH_SYS_PWROK, 1);
		/* PCH will now release PLT_RST */
	}
}

/* Chipset specific power state machine handler */

/* TODO: Separate with and without power sequencer logic here */

void s0_action_handler(const struct common_pwrseq_config *com_cfg)
{
	int ret;

	/* Handle DSW_PWROK passthrough */
	/* This is not needed for alderlake silego, guarded by CONFIG? */

	/* Check ALL_SYS_PWRGD and take action */
	ret = all_sys_pwrgd_handler();
	if (ret) {
		LOG_DBG("ALL_SYS_PWRGD handling failed err= %d", ret);
		return;
	}

	/* Send PCH_PWROK->SoC if conditions met */
	/* TODO: There is possibility of EC not needing to generate
	 * this as power sequencer may do it
	 */
	ret = generate_pch_pwrok_handler(chip_cfg.pch_pwrok_delay_ms);
	if (ret) {
		LOG_DBG("PCH_PWROK handling failed err=%d", ret);
		return;
	}

	/* SYS_PWROK may be optional and the delay must be
	 * configurable as it is variable with platform
	 */
	/* Send SYS_PWROK->SoC if conditions met */
	generate_sys_pwrok_handler(com_cfg);
}

void intel_x86_sys_reset_delay(void)
{
	/*
	 * Debounce time for SYS_RESET_L is 16 ms. Wait twice that period
	 * to be safe.
	 */
	k_msleep(chip_cfg.sys_reset_delay_ms);
}

void ap_power_reset(enum ap_power_shutdown_reason reason)
{
	/*
	 * Irrespective of cold_reset value, always toggle SYS_RESET_L to
	 * perform an AP reset. RCIN# which was used earlier to trigger
	 * a warm reset is known to not work in certain cases where the CPU
	 * is in a bad state (crbug.com/721853).
	 *
	 * The EC cannot control warm vs cold reset of the AP using
	 * SYS_RESET_L; it's more of a request.
	 */
	LOG_DBG("%s: %d", __func__, reason);

	/*
	 * Toggling SYS_RESET_L will not have any impact when it's already
	 * low (i,e. AP is in reset state).
	 */
	if (power_signal_get(PWR_SYS_RST)) {
		LOG_DBG("Chipset is in reset state");
		return;
	}

	power_signal_set(PWR_SYS_RST, 1);
	intel_x86_sys_reset_delay();
	power_signal_set(PWR_SYS_RST, 0);
	ap_power_ev_send_callbacks(AP_POWER_RESET);
}

__attribute__((weak)) void ap_power_force_shutdown(
	enum ap_power_shutdown_reason reason)
{
	int timeout_ms = 50;

	/* TODO: below
	 * report_ap_reset(reason);
	 */

	/* Turn off RMSRST_L  to meet tPCH12 */
	LOG_INF("Turning on PWR_EC_PCH_RSMRST");
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	/* Turn off S5 rails */
	LOG_INF("Turning off PWR_EN_PP5000_A");
	power_signal_set(PWR_EN_PP5000_A, 0);

	/*
	 * TODO(b/179519791): Replace this wait with
	 * power_wait_signals_timeout()
	 */
	/* Now wait for DSW_PWROK and  RSMRST_ODL to go away. */
	while (power_signal_get(PWR_DSW_PWROK) &&
			(power_signal_get(PWR_RSMRST) == 0) &&
			(timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	};

	if (!timeout_ms)
		LOG_DBG("DSW_PWROK or RSMRST_ODL didn't go low!  Assuming G3.");
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
}

__attribute__((weak)) void g3s5_action_handler(int delay, int signal_timeout)
{
	power_signal_set(PWR_EN_PP5000_A, 1);
}

void s3s0_action_handler(void)
{
}

void s0s3_action_handler(void)
{
	ap_off();
}

void init_chipset_pwr_seq_state(void)
{
	/* Deassert reset pin */
	power_signal_set(PWR_SYS_RST, 0);
}


enum power_states_ndsx chipset_pwr_sm_run(enum power_states_ndsx curr_state,
				 const struct common_pwrseq_config *com_cfg)
{
	/* Add chipset specific state handling if any */
	switch (curr_state) {
	case SYS_POWER_STATE_G3S5:
		g3s5_action_handler(com_cfg->pch_dsw_pwrok_delay_ms,
				    com_cfg->wait_signal_timeout_ms);
		break;
	case SYS_POWER_STATE_S5:
		break;
	case SYS_POWER_STATE_S3S0:
		s3s0_action_handler();
		break;
	case SYS_POWER_STATE_S0S3:
		s0s3_action_handler();
		break;
	case SYS_POWER_STATE_S0:
		s0_action_handler(com_cfg);
		break;
	default:
		break;
	}
	return curr_state;
}
