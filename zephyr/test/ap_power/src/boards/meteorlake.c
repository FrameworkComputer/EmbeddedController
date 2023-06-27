/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power_interface.h>
#include <ap_power_override_functions.h>
#include <power_signals.h>
#include <timer.h>
#include <x86_power_signals.h>

void board_ap_power_force_shutdown(void)
{
	/* Turn off PCH_RMSRST to meet tPCH12 */
	power_signal_set(PWR_EC_PCH_RSMRST, 0);

	/* Turn off PRIM load switch. */
	power_signal_set(PWR_EN_PP3300_A, 0);
}

void board_ap_power_action_g3_s5(void)
{
	/* Turn on the PP3300_PRIM rail. */
	power_signal_set(PWR_EN_PP3300_A, 1);

	if (!power_wait_signals_timeout(
		    IN_PGOOD_ALL_CORE,
		    AP_PWRSEQ_DT_VALUE(wait_signal_timeout))) {
	}
}

bool board_ap_power_check_power_rails_enabled(void)
{
	return power_signal_get(PWR_EN_PP3300_A);
}
