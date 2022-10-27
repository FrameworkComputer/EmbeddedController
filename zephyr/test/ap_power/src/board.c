/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <ap_power_override_functions.h>
#include <ap_power/ap_power_interface.h>
#include <power_signals.h>
#include <timer.h>
#include <x86_power_signals.h>

static bool signal_PWR_ALL_SYS_PWRGD;
static bool signal_PWR_DSW_PWROK;
static bool signal_PWR_PG_PP1P05;

int board_power_signal_set(enum power_signal signal, int value)
{
	switch (signal) {
	default:
		zassert_unreachable("Unknown signal");
		return -1;

	case PWR_ALL_SYS_PWRGD:
		signal_PWR_ALL_SYS_PWRGD = value;
		return 0;

	case PWR_DSW_PWROK:
		signal_PWR_DSW_PWROK = value;
		return 0;

	case PWR_PG_PP1P05:
		signal_PWR_PG_PP1P05 = value;
		return 0;
	}
}

int board_power_signal_get(enum power_signal signal)
{
	switch (signal) {
	default:
		zassert_unreachable("Unknown signal");
		return -1;

	case PWR_ALL_SYS_PWRGD:
		return signal_PWR_ALL_SYS_PWRGD;

	case PWR_DSW_PWROK:
		return signal_PWR_DSW_PWROK;

	case PWR_PG_PP1P05:
		return signal_PWR_PG_PP1P05;
	}
}

void board_ap_power_force_shutdown(void)
{
}

int board_ap_power_assert_pch_power_ok(void)
{
	power_signal_set(PWR_PCH_PWROK, 1);
	return 0;
}

static void generate_ec_soc_dsw_pwrok_handler(void)
{
	int in_sig_val = power_signal_get(PWR_DSW_PWROK);

	if (in_sig_val != power_signal_get(PWR_EC_SOC_DSW_PWROK)) {
		power_signal_set(PWR_EC_SOC_DSW_PWROK, 1);
	}
}

void board_ap_power_action_g3_s5(void)
{
	power_signal_enable(PWR_DSW_PWROK);

	power_signal_set(PWR_EN_PP3300_A, 1);
	power_signal_set(PWR_EN_PP5000_A, 1);

	power_wait_signals_timeout(IN_PGOOD_ALL_CORE, 100 * MSEC);

	generate_ec_soc_dsw_pwrok_handler();
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

bool board_ap_power_check_power_rails_enabled(void)
{
	return true;
}
