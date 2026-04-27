/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio.h"
#include "gpio_signal.h"
#include "include/system.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <ap_power/ap_power_interface.h>
#include <ap_power/ap_pwrseq.h>
#include <ap_power/ap_pwrseq_sm.h>
#include <ap_power_override_functions.h>
#include <power_signals.h>
#include <x86_power_signals.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

void board_ap_power_force_shutdown(void)
{
	power_signal_set(PWR_EC_PCH_RSMRST, 1);
	power_signal_set(PWR_EN_PP3300_A, 0);
	power_signal_set(PWR_EN_PP5000_A, 0);
}

int board_ap_power_action_g3_entry(void *data)
{
	board_ap_power_force_shutdown();
	return 0;
}

static int board_ap_power_action_g3_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_STARTUP)) {
		power_signal_set(PWR_EN_PP5000_A, 1);
		power_signal_set(PWR_EN_PP3300_A, 1);

		update_ap_boot_time(ARAIL);
	}

	if (power_signal_get(PWR_EN_PP3300_A) &&
	    power_signal_get(PWR_EN_PP5000_A) &&
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pwr_1p25v_pg)))
		return 0;

	return 1;
}
AP_POWER_APP_STATE_DEFINE(G3, board_ap_power_action_g3_entry,
			  board_ap_power_action_g3_run, NULL);

int board_power_signal_get(enum power_signal signal)
{
	switch (signal) {
	case PWR_EC_PCH_SYS_PWROK:
		return power_signal_get(PWR_PCH_PWROK);
	case PWR_SYS_RST:
		return gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(gpio_sys_rst_odl));
	default:
		return -EINVAL;
	}
}

int power_signal_external_init(void)
{
	return 0;
}

int board_power_signal_set(enum power_signal signal, int value)
{
	switch (signal) {
	case PWR_SYS_RST:
		return gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_rst_odl),
				       value);
	default:
		return -EINVAL;
	}
}
