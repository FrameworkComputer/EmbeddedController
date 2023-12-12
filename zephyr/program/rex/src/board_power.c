/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio.h"
#include "gpio_signal.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <ap_power/ap_power_interface.h>
#include <ap_power_override_functions.h>
#include <power_signals.h>
#include <x86_power_signals.h>

#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include "ap_power/ap_pwrseq_sm.h"
#endif

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#if defined(CONFIG_X86_NON_DSX_PWRSEQ_MTL) || \
	defined(CONFIG_TEST_X86_NON_DSX_PWRSEQ_MTL)
#define X86_NON_DSX_MTL_FORCE_SHUTDOWN_TO_MS 50

void board_ap_power_force_shutdown(void)
{
	int timeout_ms = X86_NON_DSX_MTL_FORCE_SHUTDOWN_TO_MS;

	/* Turn off PCH_RMSRST to meet tPCH12 */
	power_signal_set(PWR_EC_PCH_RSMRST, 0);

	/* Turn off PRIM load switch. */
	power_signal_set(PWR_EN_PP3300_A, 0);

	/* Wait RSMRST to be off. */
	while (power_signal_get(PWR_RSMRST) && (timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	};

	if (power_signal_get(PWR_RSMRST)) {
		LOG_WRN("RSMRST_ODL didn't go low!  Assuming G3.");
	}
}

#ifndef CONFIG_AP_PWRSEQ_DRIVER
void board_ap_power_action_g3_s5(void)
{
	/* Turn on the PP3300_PRIM rail. */
	power_signal_set(PWR_EN_PP3300_A, 1);

	update_ap_boot_time(ARAIL);

	if (!power_wait_signals_timeout(
		    IN_PGOOD_ALL_CORE,
		    AP_PWRSEQ_DT_VALUE(wait_signal_timeout))) {
		ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	}
}

bool board_ap_power_check_power_rails_enabled(void)
{
	return power_signal_get(PWR_EN_PP3300_A);
}
#else
int board_ap_power_action_g3_entry(void *data)
{
	board_ap_power_force_shutdown();

	return 0;
}

static int board_ap_power_action_g3_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_STARTUP)) {
		/* Turn on the PP3300_PRIM rail. */
		power_signal_set(PWR_EN_PP3300_A, 1);
	}

	if (!power_signal_get(PWR_EN_PP3300_A)) {
		return 1;
	}

	return 0;
}

AP_POWER_APP_STATE_DEFINE(AP_POWER_STATE_G3, board_ap_power_action_g3_entry,
			  board_ap_power_action_g3_run, NULL);
#endif /* CONFIG_AP_PWRSEQ_DRIVER */
#endif /* CONFIG_X86_NON_DSX_PWRSEQ_MTL */
