/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio.h"
#include "gpio_signal.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <ap_power/ap_power_interface.h>
#include <ap_power_override_functions.h>
#include <power_signals.h>
#include <x86_power_signals.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_SHUTDOWN_TO_MS 5

void board_ap_power_force_shutdown(void)
{
	int timeout_ms = X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_SHUTDOWN_TO_MS;

	power_signal_set(PWR_EC_SOC_DSW_PWROK, 0);
	power_signal_set(PWR_EC_PCH_RSMRST, 0);

	/* TODO: replace with power_wait_signals_timeout()? */
	while (power_signal_get(PWR_RSMRST) == 0 &&
	       power_signal_get(PWR_SLP_SUS) == 0 && timeout_ms > 0) {
		k_msleep(1);
		timeout_ms--;
	}
	if (power_signal_get(PWR_SLP_SUS) == 0) {
		LOG_WRN("SLP_SUS is not deasserted! Assuming G3");
	}

	if (power_signal_get(PWR_RSMRST) == 1) {
		LOG_WRN("RSMRST is not deasserted! Assuming G3");
	}

	power_signal_set(PWR_EN_PP5000_A, 0);
}

void board_ap_power_action_g3_s5(void)
{
	LOG_DBG("Turning on EN_S5_RAILS");
	power_signal_set(PWR_EN_PP5000_A, 1);

	update_ap_boot_time(ARAIL);

	/* Assert DSW_PWROK after 3.3V rail stable.  No power good
	 * signal is available, so use a fixed delay.
	 */
	k_msleep(AP_PWRSEQ_DT_VALUE(dsw_pwrok_delay));
	power_signal_set(PWR_EC_SOC_DSW_PWROK, 1);
}

void board_ap_power_action_s3_s0(void)
{
}

void board_ap_power_action_s0_s3(void)
{
}

void board_ap_power_action_s0(void)
{
}

int board_ap_power_assert_pch_power_ok(void)
{
	int retry = 0;

	/* RPL PDG indicates to gate PCH_PWROK on VR_READY and ALL_SYS_PWRGD.
	 * The common ADL code has already asserted ALL_SYS_PWRGD.
	 */
	while (power_signal_get(PWR_IMVP9_VRRDY) == 0) {
		/*
		 * There isn't an documented ramp time for the IMVP9.
		 * 20ms retry chosen arbitrarily.
		 */
		if (++retry > 2) {
			LOG_ERR("IMVP9_VRRDY_OD timeout");
			return -1;
		}
		k_msleep(10);
	}

	power_signal_set(PWR_PCH_PWROK, 1);

	return 0;
}

bool board_ap_power_check_power_rails_enabled(void)
{
	return power_signal_get(PWR_EN_PP5000_A) &&
	       power_signal_get(PWR_EC_SOC_DSW_PWROK);
}

int board_power_signal_get(enum power_signal signal)
{
	switch (signal) {
	default:
		LOG_ERR("Unknown signal for board get: %d", signal);
		return -EINVAL;
	case PWR_DSW_PWROK:
		/* fall through */
	case PWR_PG_PP1P05:
		/* Brox doesn't provide a power good or ADC measurement
		 * of the PP3300 rail.  Return the state of the enable signal
		 * for the PP5000/PP3300 rails.
		 */
		return power_signal_get(PWR_EN_PP5000_A);
	}
}

int board_power_signal_set(enum power_signal signal, int value)
{
#ifndef CONFIG_ZTEST
	return -EINVAL;
#else
	/*
	 * PWR_DSW_PWROK and PWR_PG_PP1P05 are input only signals. However,
	 * the power sequence test harness requires the set operation to
	 * succeed. As the read value of both signals is based on main power
	 * rail enable, make this a no-op.
	 *
	 * This leaves the real implementation untested, but we get good
	 * coverage for board_power_signal_set().
	 */
	return 0;
#endif
}
