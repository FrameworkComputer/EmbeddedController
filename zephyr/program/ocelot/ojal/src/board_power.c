/* Copyright 2025 The ChromiumOS Authors
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

/*
 * PWR_ALL_SYS_PWRGD is designed as an output pin. Forwards the VCCST_PWRGD
 * state to the ALL_SYS_PWRGD signal and posts a power signal event to the AP
 * power state machine.
 */
void board_all_sys_pwrgd_interrupt(const struct device *unused_device,
				   struct gpio_callback *unused_callback,
				   gpio_port_pins_t unused_pin)
{
	if (power_signal_get(PWR_VCCST_PWRGD))
		power_signal_set(PWR_ALL_SYS_PWRGD, 1);
	else
		power_signal_set(PWR_ALL_SYS_PWRGD, 0);

	ap_pwrseq_post_event(ap_pwrseq_get_instance(),
			     AP_PWRSEQ_EVENT_POWER_SIGNAL);
}

static int board_config_pwrgd_interrupt(void)
{
	const struct gpio_dt_spec *const pwrgd_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_vr_mem_vccst_pg);
	static struct gpio_callback cb;
	int rv;

	gpio_init_callback(&cb, board_all_sys_pwrgd_interrupt,
			   BIT(pwrgd_gpio->pin));
	gpio_add_callback(pwrgd_gpio->port, &cb);

	rv = gpio_pin_interrupt_configure_dt(pwrgd_gpio, GPIO_INT_EDGE_BOTH);
	__ASSERT(rv == 0,
		 "vr_mem_vccst_pg interrupt configuration returned error %d",
		 rv);

	return 0;
}
SYS_INIT(board_config_pwrgd_interrupt, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);
