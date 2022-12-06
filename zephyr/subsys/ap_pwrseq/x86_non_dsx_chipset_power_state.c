/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_power_events.h>
#include <ap_power/ap_pwrseq_sm.h>
#endif
#include <x86_common_pwrseq.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

/**
 * Determine the current state of the CPU from the
 * power signals.
 */
#ifndef CONFIG_AP_PWRSEQ_DRIVER
enum power_states_ndsx chipset_pwr_seq_get_state(void)
{
	power_signal_mask_t sig = power_get_signals();

	/*
	 * Chip is shut down, G3 state.
	 */
	if ((sig & MASK_ALL_POWER_GOOD) == 0) {
		LOG_DBG("All power rails off, G3 state");
		return SYS_POWER_STATE_G3;
	}
	/*
	 * Not enough power rails up to read VW signals.
	 * Force a shutdown.
	 */
	if ((sig & MASK_VW_POWER) != VALUE_VW_POWER) {
		LOG_ERR("Not enough power signals on (%#x), forcing shutdown",
			sig);
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
		return SYS_POWER_STATE_G3;
	}

	/*
	 * Enough power signals are up, so
	 * wait for virtual wire signals to become available.
	 * Not sure how long to wait? 5 seconds total.
	 */
	for (int delay = 0; delay < 500; k_msleep(10), delay++) {
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S3)
		if (power_signal_get(PWR_SLP_S3) < 0)
			continue;
#endif
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S4)
		if (power_signal_get(PWR_SLP_S4) < 0)
			continue;
#endif
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S5)
		if (power_signal_get(PWR_SLP_S5) < 0)
			continue;
#endif
		/*
		 * All signals valid.
		 */
		LOG_DBG("All VW signals valid after %d ms", delay * 10);
		break;
	}
	/* Re-read the power signals */
	sig = power_get_signals();

	/*
	 * S0, all power OK, no suspend or sleep on.
	 */
	if ((sig & MASK_S0) == VALUE_S0) {
		LOG_DBG("CPU in S0 state");
		return SYS_POWER_STATE_S0;
	}
	/*
	 * S3, all power OK, PWR_SLP_S3 on.
	 */
	if ((sig & MASK_S3) == VALUE_S3) {
		LOG_DBG("CPU in S3 state");
		return SYS_POWER_STATE_S3;
	}
	/*
	 * S5, some power signals on, PWR_SLP_S5 on.
	 */
	if ((sig & MASK_S5) == VALUE_S5) {
		LOG_DBG("CPU in S5 state");
		return SYS_POWER_STATE_S5;
	}
	/*
	 * Unable to determine state, force to G3.
	 */
	LOG_INF("Unable to determine CPU state (%#x), forcing shutdown", sig);
	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	return SYS_POWER_STATE_G3;
}
#else
static void x86_non_dsx_chipset_state_entry_cb(const struct device *dev,
					       const enum ap_pwrseq_state entry,
					       const enum ap_pwrseq_state exit)
{
	switch (entry) {
	case AP_POWER_STATE_G3:
		if (exit >= AP_POWER_STATE_S5) {
			ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
		}
		ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
		ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN_COMPLETE);
		break;

	case AP_POWER_STATE_S3:
		ap_power_ev_send_callbacks(AP_POWER_STARTUP);
		break;

	case AP_POWER_STATE_S0:
		/* Notify power event rails are up */
		ap_power_ev_send_callbacks(AP_POWER_RESUME);
		break;

#if CONFIG_AP_PWRSEQ_S0IX
	case AP_POWER_STATE_S0IX:
		ap_power_ev_send_callbacks(AP_POWER_S0IX_SUSPEND);
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND_COMPLETE);
#endif
		break;
#endif

	default:
		break;
	}
}

static void x86_non_dsx_chipset_state_exit_cb(const struct device *dev,
					      const enum ap_pwrseq_state entry,
					      const enum ap_pwrseq_state exit)
{
	switch (exit) {
	case AP_POWER_STATE_G3:
		ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
		break;

	case AP_POWER_STATE_S3:
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		if (entry == AP_POWER_STATE_S0) {
			/* Notify power event before resume */
			ap_power_ev_send_callbacks(AP_POWER_RESUME_INIT);
		}
#endif
		break;

	case AP_POWER_STATE_S0:
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		if (entry == AP_POWER_STATE_S3) {
			/* Notify power event after suspend */
			ap_power_ev_send_callbacks(AP_POWER_SUSPEND_COMPLETE);
		}
#endif
		break;

#if CONFIG_AP_PWRSEQ_S0IX
	case AP_POWER_STATE_S0IX:
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		ap_power_ev_send_callbacks(AP_POWER_RESUME_INIT);
#endif
		ap_power_ev_send_callbacks(AP_POWER_S0IX_RESUME);
		break;
#endif

	default:
		break;
	}
}

static int x86_non_dsx_chipset_init_events(const struct device *dev)
{
	static struct ap_pwrseq_state_callback ap_pwrseq_entry_cb;
	static struct ap_pwrseq_state_callback ap_pwrseq_exit_cb;
	const struct device *ap_pwrseq_dev = ap_pwrseq_get_instance();

	ARG_UNUSED(dev);
	power_signal_init();

	ap_pwrseq_entry_cb.cb = x86_non_dsx_chipset_state_entry_cb;
	ap_pwrseq_entry_cb.states_bit_mask =
		(BIT(AP_POWER_STATE_G3) | BIT(AP_POWER_STATE_S3) |
		 BIT(AP_POWER_STATE_S0)
#if CONFIG_AP_PWRSEQ_S0IX
		 | BIT(AP_POWER_STATE_S0IX)
#endif
		);

	ap_pwrseq_register_state_entry_callback(ap_pwrseq_dev,
						&ap_pwrseq_entry_cb);

	ap_pwrseq_exit_cb.cb = x86_non_dsx_chipset_state_exit_cb;
	ap_pwrseq_exit_cb.states_bit_mask =
		(BIT(AP_POWER_STATE_G3) | BIT(AP_POWER_STATE_S3) |
		 BIT(AP_POWER_STATE_S0)
#if CONFIG_AP_PWRSEQ_S0IX
		 | BIT(AP_POWER_STATE_S0IX)
#endif
		);

	ap_pwrseq_register_state_exit_callback(ap_pwrseq_dev,
					       &ap_pwrseq_exit_cb);

	ap_power_ev_send_callbacks(AP_POWER_INITIALIZED);

	return 0;
}
SYS_INIT(x86_non_dsx_chipset_init_events, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);

enum ap_pwrseq_state chipset_pwr_seq_get_state(void)
{
	power_signal_mask_t sig;

	sig = power_get_signals();
	/*
	 * Chip is shut down, G3 state.
	 */
	if ((sig & MASK_ALL_POWER_GOOD) == 0) {
		LOG_DBG("All power rails off, G3 state");
		return AP_POWER_STATE_G3;
	}
	/*
	 * Not enough power rails up to read VW signals.
	 * Force a shutdown.
	 */
	if ((sig & MASK_VW_POWER) != VALUE_VW_POWER) {
		LOG_ERR("Not enough power signals on (%#x), forcing shutdown",
			sig);
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
		return AP_POWER_STATE_G3;
	}

	/*
	 * Enough power signals are up, so
	 * wait for virtual wire signals to become available.
	 * Not sure how long to wait? 5 seconds total.
	 */
	for (int delay = 0; delay < 500; k_msleep(10), delay++) {
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S3)
		if (power_signal_get(PWR_SLP_S3) < 0)
			continue;
#endif
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S4)
		if (power_signal_get(PWR_SLP_S4) < 0)
			continue;
#endif
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S5)
		if (power_signal_get(PWR_SLP_S5) < 0)
			continue;
#endif
		/*
		 * All signals valid.
		 */
		LOG_DBG("All VW signals valid after %d ms", delay * 10);
		break;
	}
	/* Re-read the power signals */
	sig = power_get_signals();

	/*
	 * S0, all power OK, no suspend or sleep on.
	 */
	if ((sig & MASK_S0) == VALUE_S0) {
		LOG_DBG("CPU in S0 state");
		return AP_POWER_STATE_S0;
	}
	/*
	 * S3, all power OK, PWR_SLP_S3 on.
	 */
	if ((sig & MASK_S3) == VALUE_S3) {
		LOG_DBG("CPU in S3 state");
		return AP_POWER_STATE_S3;
	}
	/*
	 * S5, some power signals on, PWR_SLP_S5 on.
	 */
	if ((sig & MASK_S5) == VALUE_S5) {
		LOG_DBG("CPU in S5 state");
		return AP_POWER_STATE_S5;
	}
	/*
	 * Unable to determine state, force to G3.
	 */
	LOG_INF("Unable to determine CPU state (%#x), forcing shutdown", sig);
	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	return AP_POWER_STATE_G3;
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */
