/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

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

#define X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_SHUTDOWN_TO_MS 5

#ifndef CONFIG_AP_PWRSEQ_DRIVER
test_export_static bool s0_stable;
#endif

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

#ifndef CONFIG_AP_PWRSEQ_DRIVER
	if (s0_stable) {
		/* Enable these power signals in case of sudden shutdown */
		power_signal_enable(PWR_DSW_PWROK);
		power_signal_enable(PWR_PG_PP1P05);
	}
#endif

	power_signal_set(PWR_EC_SOC_DSW_PWROK, 0);
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	while (power_signal_get(PWR_RSMRST_PWRGD) == 1 &&
	       power_signal_get(PWR_SLP_SUS) == 0 && timeout_ms > 0) {
		k_msleep(1);
		timeout_ms--;
	}

	/* LCOV_EXCL_START messages are informational only */
	if (power_signal_get(PWR_SLP_SUS) == 0) {
		LOG_WRN("SLP_SUS is not asserted! Assuming G3");
	}
	if (power_signal_get(PWR_RSMRST_PWRGD) == 1) {
		LOG_WRN("RSMRST_PWRGD is asserted! Assuming G3");
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
#ifndef CONFIG_AP_PWRSEQ_DRIVER
	s0_stable = false;
#endif
}

#ifndef CONFIG_AP_PWRSEQ_DRIVER
void board_ap_power_action_g3_s5(void)
{
	power_signal_enable(PWR_DSW_PWROK);
	power_signal_enable(PWR_PG_PP1P05);

	LOG_DBG("Turning on PWR_EN_PP5000_A and PWR_EN_PP3300_A");
	power_signal_set(PWR_EN_PP5000_A, 1);
	power_signal_set(PWR_EN_PP3300_A, 1);

	update_ap_boot_time(ARAIL);
	power_wait_signals_on_timeout(IN_PGOOD_ALL_CORE,
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
#else
#ifndef CONFIG_EMUL_AP_PWRSEQ_DRIVER
/* This is called by AP Power Sequence driver only when AP exits S0 or S0IX */
static void board_ap_power_cb(const struct device *dev,
			      const enum ap_pwrseq_state entry,
			      const enum ap_pwrseq_state exit)
{
	if (entry == AP_POWER_STATE_S0IX) {
		/* Avoid enabling signals when entering S0IX */
		return;
	}
	power_signal_enable(PWR_DSW_PWROK);
	power_signal_enable(PWR_PG_PP1P05);
}

static int board_ap_power_init(void)
{
	const struct device *ap_pwrseq_dev = ap_pwrseq_get_instance();
	static struct ap_pwrseq_state_callback exit_cb = {
		.cb = board_ap_power_cb,
		.states_bit_mask =
			(BIT(AP_POWER_STATE_S0) | BIT(AP_POWER_STATE_S0IX)),
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
		power_signal_enable(PWR_DSW_PWROK);
		power_signal_enable(PWR_PG_PP1P05);

		LOG_INF("Turning on PWR_EN_PP5000_A and PWR_EN_PP3300_A");

		power_signal_set(PWR_EN_PP5000_A, 1);
		power_signal_set(PWR_EN_PP3300_A, 1);

		power_wait_signals_on_timeout(
			POWER_SIGNAL_MASK(PWR_DSW_PWROK),
			AP_PWRSEQ_DT_VALUE(wait_signal_timeout));
	}

	generate_ec_soc_dsw_pwrok_handler(AP_PWRSEQ_DT_VALUE(dsw_pwrok_delay));

	if (power_signal_get(PWR_EN_PP5000_A) &&
	    power_signal_get(PWR_EN_PP3300_A) &&
	    power_signal_get(PWR_EC_SOC_DSW_PWROK)) {
		return 0;
	}

	return 1;
}

AP_POWER_APP_STATE_DEFINE(AP_POWER_STATE_G3, board_ap_power_g3_entry,
			  board_ap_power_g3_run, NULL);

static int board_ap_power_s0_run(void *data)
{
	if (power_signal_get(PWR_ALL_SYS_PWRGD) &&
	    power_signal_get(PWR_VCCST_PWRGD) &&
	    power_signal_get(PWR_PCH_PWROK) &&
	    power_signal_get(PWR_EC_PCH_SYS_PWROK)) {
		/*
		 * Make sure all the signals checked inside the condition are
		 * asserted before disabling these two power signals.
		 */
		power_signal_disable(PWR_DSW_PWROK);
		power_signal_disable(PWR_PG_PP1P05);
	}

	return 0;
}

AP_POWER_APP_STATE_DEFINE(AP_POWER_STATE_S0, NULL, board_ap_power_s0_run, NULL);
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

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
