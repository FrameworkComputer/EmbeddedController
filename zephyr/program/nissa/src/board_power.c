/* Copyright 2022 The ChromiumOS Authors
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
#include <x86_power_signals.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_SHUTDOWN_TO_MS 5

test_export_static bool s0_stable;

static void generate_ec_soc_dsw_pwrok_handler(int delay)
{
	int in_sig_val = power_signal_get(PWR_DSW_PWROK);

	if (in_sig_val != power_signal_get(PWR_EC_SOC_DSW_PWROK)) {
		if (in_sig_val)
			k_msleep(delay);
		power_signal_set(PWR_EC_SOC_DSW_PWROK, 1);
	}
}

void board_ap_power_force_shutdown(void)
{
	int timeout_ms = X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_SHUTDOWN_TO_MS;

	if (s0_stable) {
		/* Enable these power signals in case of sudden shutdown */
		power_signal_enable(PWR_DSW_PWROK);
		power_signal_enable(PWR_PG_PP1P05);
	}

	power_signal_set(PWR_EC_SOC_DSW_PWROK, 0);
	power_signal_set(PWR_EC_PCH_RSMRST, 0);

	while (power_signal_get(PWR_RSMRST) == 0 &&
	       power_signal_get(PWR_SLP_SUS) == 0 && timeout_ms > 0) {
		k_msleep(1);
		timeout_ms--;
	}

	/* LCOV_EXCL_START messages are informational only */
	if (power_signal_get(PWR_SLP_SUS) == 0) {
		LOG_WRN("SLP_SUS is not deasserted! Assuming G3");
	}
	if (power_signal_get(PWR_RSMRST) == 1) {
		LOG_WRN("RSMRST is not deasserted! Assuming G3");
	}
	/* LCOV_EXCL_STOP */

	power_signal_set(PWR_EN_PP3300_A, 0);

	power_signal_set(PWR_EN_PP5000_A, 0);

	timeout_ms = X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_SHUTDOWN_TO_MS;
	while (power_signal_get(PWR_DSW_PWROK) && timeout_ms > 0) {
		k_msleep(1);
		timeout_ms--;
	};

	/* LCOV_EXCL_START informational */
	if (power_signal_get(PWR_DSW_PWROK))
		LOG_WRN("DSW_PWROK didn't go low!  Assuming G3.");
	/* LCOV_EXCL_STOP */

	power_signal_disable(PWR_DSW_PWROK);
	power_signal_disable(PWR_PG_PP1P05);
	s0_stable = false;
}

void board_ap_power_action_g3_s5(void)
{
	power_signal_enable(PWR_DSW_PWROK);
	power_signal_enable(PWR_PG_PP1P05);

	LOG_DBG("Turning on PWR_EN_PP5000_A and PWR_EN_PP3300_A");
	power_signal_set(PWR_EN_PP5000_A, 1);
	power_signal_set(PWR_EN_PP3300_A, 1);

	update_ap_boot_time(ARAIL);
	power_wait_signals_timeout(IN_PGOOD_ALL_CORE,
				   AP_PWRSEQ_DT_VALUE(wait_signal_timeout));

	generate_ec_soc_dsw_pwrok_handler(AP_PWRSEQ_DT_VALUE(dsw_pwrok_delay));
	s0_stable = false;
}

void board_ap_power_action_s3_s0(void)
{
	s0_stable = false;
}

void board_ap_power_action_s0_s3(void)
{
	power_signal_enable(PWR_DSW_PWROK);
	power_signal_enable(PWR_PG_PP1P05);
	s0_stable = false;
}

void board_ap_power_action_s0(void)
{
	if (s0_stable) {
		return;
	}
	LOG_INF("Reaching S0");
	power_signal_disable(PWR_DSW_PWROK);
	power_signal_disable(PWR_PG_PP1P05);
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
	       power_signal_get(PWR_EC_SOC_DSW_PWROK);
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
		if (!power_signal_get(PWR_PG_PP1P05)) {
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
