/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio.h"
#include "gpio_signal.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#ifndef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#else
#include "ap_power/ap_pwrseq_sm.h"
#endif
#include <ap_power/ap_power_interface.h>
#include <ap_power/ap_pwrseq.h>
#include <ap_power_override_functions.h>
#include <power_signals.h>
#include <x86_power_signals.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#ifndef CONFIG_AP_PWRSEQ_DRIVER
test_export_static bool s0_stable;
#endif

void board_ap_power_force_shutdown(void)
{
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	power_signal_set(PWR_EN_PP3300_A, 0);

	power_signal_set(PWR_EN_PP5000_A, 0);

#ifndef CONFIG_AP_PWRSEQ_DRIVER
	s0_stable = false;
#endif
}

#ifndef CONFIG_AP_PWRSEQ_DRIVER
void board_ap_power_action_g3_s5(void)
{
	LOG_DBG("Turning on PWR_EN_PP5000_A and PWR_EN_PP3300_A");
	power_signal_set(PWR_EN_PP5000_A, 1);
	power_signal_set(PWR_EN_PP3300_A, 1);

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
	return power_signal_get(PWR_EN_PP3300_A) &&
	       power_signal_get(PWR_EN_PP5000_A) &&
	       gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pwr_1p25v_pg));
}
#else
#ifndef CONFIG_EMUL_AP_PWRSEQ_DRIVER
/* This is called by AP Power Sequence driver only when AP exits S0 or S0IX */
static void board_ap_power_cb(const struct device *dev,
			      const enum ap_pwrseq_state entry,
			      const enum ap_pwrseq_state exit)
{
	if (entry == AP_POWER_STATE_S0ix) {
		/* Avoid enabling signals when entering S0IX */
		return;
	}
}

static int board_ap_power_init(void)
{
	const struct device *ap_pwrseq_dev = ap_pwrseq_get_instance();
	static struct ap_pwrseq_state_callback exit_cb = {
		.cb = board_ap_power_cb,
		.states_bit_mask =
			(BIT(AP_POWER_STATE_S0) | BIT(AP_POWER_STATE_S0ix)),
	};

	ap_pwrseq_register_state_exit_callback(ap_pwrseq_dev, &exit_cb);

	return 0;
}
SYS_INIT(board_ap_power_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif /* CONFIG_EMUL_AP_PWRSEQ_DRIVER */

static int board_ap_power_g3_entry(void *data)
{
	board_ap_power_force_shutdown();

	return 0;
}

static int board_ap_power_g3_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_STARTUP)) {
		LOG_INF("Turning on PWR_EN_PP5000_A and PWR_EN_PP3300_A");

		power_signal_set(PWR_EN_PP5000_A, 1);
		power_signal_set(PWR_EN_PP3300_A, 1);

		power_wait_signals_on_timeout(
			AP_PWRSEQ_DT_VALUE(wait_signal_timeout));
	}

	if (power_signal_get(PWR_EN_PP5000_A) &&
	    power_signal_get(PWR_EN_PP3300_A)) {
		return 0;
	}

	return 1;
}

AP_POWER_APP_STATE_DEFINE(G3, board_ap_power_g3_entry, board_ap_power_g3_run,
			  NULL);

static int board_ap_power_s0_run(void *data)
{
	return 0;
}

AP_POWER_APP_STATE_DEFINE(S0, NULL, board_ap_power_s0_run, NULL);
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

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
#ifndef CONFIG_AP_PWRSEQ_DRIVER
	ap_pwrseq_wake();
#else
	ap_pwrseq_post_event(ap_pwrseq_get_instance(),
			     AP_PWRSEQ_EVENT_POWER_SIGNAL);
#endif
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
