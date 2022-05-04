/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_common_pwrseq.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

/**
 * Determine the current state of the CPU from the
 * power signals.
 */
enum power_states_ndsx chipset_pwr_seq_get_state(void)
{
	/*
	 * Chip is shut down.
	 */
	if ((power_get_signals() & MASK_ALL_POWER_GOOD) == 0) {
		LOG_DBG("Power rails off, G3 state");
		return SYS_POWER_STATE_G3;
	}
	/*
	 * If not all the power rails are available,
	 * then force shutdown to G3 to get to known state.
	 */
	if ((power_get_signals() & MASK_ALL_POWER_GOOD)
			!= MASK_ALL_POWER_GOOD) {
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
		LOG_INF("Not all power rails up, forcing shutdown");
		return SYS_POWER_STATE_G3;
	}

	/*
	 * All the power rails are good, so
	 * wait for virtual wire signals to become available.
	 * Not sure how long to wait? 5 seconds total.
	 */
	for (int delay = 0; delay < 500; k_msleep(10), delay++) {
#if defined(CONFIG_PLATFORM_EC_ESPI_VW_SLP_S3)
		if (power_signal_get(PWR_SLP_S3) < 0)
			continue;
#endif
#if defined(CONFIG_PLATFORM_EC_ESPI_VW_SLP_S4)
		if (power_signal_get(PWR_SLP_S4) < 0)
			continue;
#endif
#if defined(CONFIG_PLATFORM_EC_ESPI_VW_SLP_S5)
		if (power_signal_get(PWR_SLP_S5) < 0)
			continue;
#endif
		/*
		 * All signals valid.
		 */
		LOG_DBG("All VW signals valid after %d ms", delay * 10);
		break;
	}
	/*
	 * S0, all power OK, no suspend or sleep on.
	 */
	if ((power_get_signals() & MASK_S0) == MASK_ALL_POWER_GOOD) {
		LOG_DBG("CPU in S0 state");
		return SYS_POWER_STATE_S0;
	}
	/*
	 * S3, all power OK, PWR_SLP_S3 on.
	 */
	if ((power_get_signals() & MASK_S0) ==
		(MASK_ALL_POWER_GOOD | POWER_SIGNAL_MASK(PWR_SLP_S3))) {
		LOG_DBG("CPU in S3 state");
		return SYS_POWER_STATE_S3;
	}
	/*
	 * S5, all power OK, PWR_SLP_S5 on.
	 */
	if ((power_get_signals() & MASK_S5) == MASK_S5) {
		LOG_DBG("CPU in S5 state");
		return SYS_POWER_STATE_S5;
	}
	/*
	 * Unable to determine state, force to G3.
	 */
	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	LOG_INF("Unable to determine CPU state, forcing shutdown");
	return SYS_POWER_STATE_G3;
}
