/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "gpio_signal.h"
#include "include/system.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <power_signals.h>
#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_pwrseq_sm.h>
#endif

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define X86_NON_DSX_FORCE_SHUTDOWN_TO_MS 50

void board_ap_power_force_shutdown(void)
{
	int timeout_ms = X86_NON_DSX_FORCE_SHUTDOWN_TO_MS;

	/* Turn off PCH_RMSRST to meet tPCH12 */
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	/* Disable all control gpio here */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bklt_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_cpu_vr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_slp_sx_n), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_vddq_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_v1p8a_en), 0);

	/* Turn off PRIM load switch. */
	power_signal_set(PWR_EN_PP3300_A, 0);

	power_signal_set(PWR_EN_PP5000_A, 0);
	/* Wait RSMRST to be off. */
	while (power_signal_get(PWR_RSMRST_PWRGD) && (timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	};

	if (power_signal_get(PWR_RSMRST_PWRGD))
		LOG_WRN("RSMRST_PWRGD didn't go low!  Assuming G3.");
}

#ifdef CONFIG_AP_PWRSEQ_DRIVER
int board_ap_power_action_g3_entry(void *data)
{
	board_ap_power_force_shutdown();

	return 0;
}

static int board_ap_power_action_g3_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_STARTUP)) {
		power_signal_set(PWR_EN_PP5000_A, 1);
		/* Turn on the PP3300_PRIM rail. */
		power_signal_set(PWR_EN_PP3300_A, 1);
		/* Turn on PP1800_S5 rail */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_v1p8a_en), 1);

		update_ap_boot_time(ARAIL);
	}

	/* Return 0 only if power rails have been enabled  */
	return !power_signal_get(PWR_EN_PP3300_A);
}

AP_POWER_APP_STATE_DEFINE(AP_POWER_STATE_G3, board_ap_power_action_g3_entry,
			  board_ap_power_action_g3_run, NULL);

static int board_ap_power_action_s4_run(void *data)
{
	/* Disable vddq_en before goto S5 */
	if (power_signal_get(PWR_RSMRST_PWRGD) == 0 ||
	    power_signal_get(PWR_SLP_S5)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_vddq_en), 0);
		return 0;
	}

	/* Enable vddq_en before goto S3 */
	if (!power_signal_get(PWR_SLP_S4)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_vddq_en), 1);
		return 0;
	}

	return 0;
}

AP_POWER_APP_STATE_DEFINE(AP_POWER_STATE_S4, NULL, board_ap_power_action_s4_run,
			  NULL);

static int board_ap_power_action_s3_run(void *data)
{
	/* Disable GPIOs before goto S4 */
	if (power_signal_get(PWR_RSMRST_PWRGD) == 0 ||
	    power_signal_get(PWR_SLP_S4)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_codec_amp_en),
				0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_touchpad_en_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bklt_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_cpu_vr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_slp_sx_n), 0);
		return 0;
	}

	/* Enable GPIOs before goto S0 */
	if (power_signal_get(PWR_ALL_SYS_PWRGD)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_slp_sx_n), 1);
		k_msleep(2);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_cpu_vr_en), 1);
		/* Todo: enable devices power here, optimize these later */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bklt_en), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_touchpad_en_ec), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_codec_amp_en),
				1);
		return 0;
	}

	return 0;
}

AP_POWER_APP_STATE_DEFINE(AP_POWER_STATE_S3, NULL, board_ap_power_action_s3_run,
			  NULL);
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

int board_power_signal_get(enum power_signal signal)
{
	switch (signal) {
	case PWR_ALL_SYS_PWRGD:
		/*
		 * All system power is good.
		 * Checks that PWR_SLP_S3 is off, and
		 * the GPIO signal for vcc_ddr power good is set.
		 */
		if (power_signal_get(PWR_SLP_S3)) {
			return 0;
		}
		if (!gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_vcc_ddr_pwrok))) {
			return 0;
		}
		return 1;
	default:
		return -EINVAL;
	}
}

int board_power_signal_set(enum power_signal signal, int value)
{
	return -EINVAL;
}
