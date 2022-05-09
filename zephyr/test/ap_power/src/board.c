/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <ztest.h>

#include <ap_power_override_functions.h>
#include <ap_power/ap_power_interface.h>
#include <power_signals.h>

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
	return 0;
}

void board_ap_power_action_g3_s5(void)
{
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

int extpower_is_present(void)
{
	return 0;
}
