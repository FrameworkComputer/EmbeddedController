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
#include <ap_power_override_functions.h>
#include <power_signals.h>
#include <system.h>
#include <x86_power_signals.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

test_export_static bool s0_stable;

void board_ap_power_force_shutdown(void)
{
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	power_signal_set(PWR_EN_PP5000_A, 0);

	s0_stable = false;
}

void board_ap_power_action_g3_s5(void)
{
	LOG_DBG("Turning on PWR_EN_PP5000_A and PWR_EN_PP3300_A");
	power_signal_set(PWR_EN_PP5000_A, 1);

	/* Indication to soc on recovery boot */
	if (system_is_manual_recovery()) {
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rec_switch_odl), 1);
	} else {
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rec_switch_odl), 0);
	}

	update_ap_boot_time(ARAIL);
	power_wait_signals_on_timeout(IN_PGOOD_ALL_CORE,
				      AP_PWRSEQ_DT_VALUE(wait_signal_timeout));

	s0_stable = false;
}

void board_ap_power_action_s3_s0(void)
{
	s0_stable = false;
}

void board_ap_power_action_s0_s3(void)
{
	s0_stable = false;
}

void board_ap_power_action_s0(void)
{
	if (s0_stable) {
		return;
	}
	LOG_INF("Reaching S0");
	s0_stable = true;
}

int board_ap_power_assert_pch_power_ok(void)
{
	/* Pass though PCH_PWROK */
	if (power_signal_get(PWR_PCH_PWROK) == 0) {
		k_msleep(AP_PWRSEQ_DT_VALUE(pch_pwrok_delay));
		power_signal_set(PWR_PCH_PWROK, 1);
	}

	return 0;
}

bool board_ap_power_check_power_rails_enabled(void)
{
	return power_signal_get(PWR_EN_PP5000_A);
}

int power_signal_external_init(void)
{
	return 0;
}

int board_power_signal_get(enum power_signal signal)
{
	switch (signal) {
	default:
		LOG_ERR("Unknown signal for board get: %d", signal);
		return -EINVAL;

	case PWR_ALL_SYS_PWRGD:
		/*
		 * All system power is good.
		 * Checks that PWR_SLP_S3 is off, and
		 * the GPIO signal for all power good is set,
		 * and that the 1.05 volt line is ready.
		 */
		if (power_signal_get(PWR_SLP_S3)) {
			return 0;
		}
		if (!gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_all_sys_pwrgd))) {
			return 0;
		}
		return 1;
	}
}

int board_power_signal_set(enum power_signal signal, int value)
{
	return -EINVAL;
}

/*
 * As a soft power signal, PWR_ALL_SYS_PWRGD will never wake the power state
 * machine on its own. Since its value depends on the state of
 * gpio_all_sys_pwrgd, wake the state machine to re-evaluate ALL_SYS_PWRGD
 * anytime the input changes.
 */
void board_all_sys_pwrgd_interrupt(const struct device *unused_device,
				   struct gpio_callback *unused_callback,
				   gpio_port_pins_t unused_pin)
{
	ap_pwrseq_wake();
}

static int board_config_pwrgd_interrupt(void)
{
	const struct gpio_dt_spec *const pwrgd_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_all_sys_pwrgd);
	static struct gpio_callback cb;
	int rv;

	gpio_init_callback(&cb, board_all_sys_pwrgd_interrupt,
			   BIT(pwrgd_gpio->pin));
	gpio_add_callback(pwrgd_gpio->port, &cb);

	rv = gpio_pin_interrupt_configure_dt(pwrgd_gpio, GPIO_INT_EDGE_BOTH);
	__ASSERT(rv == 0,
		 "all_sys_pwrgd interrupt configuration returned error %d", rv);

	return 0;
}
SYS_INIT(board_config_pwrgd_interrupt, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);
