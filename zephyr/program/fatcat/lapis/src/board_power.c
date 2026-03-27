/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "gpio_signal.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_pwrseq_sm.h>
#include <ap_power_override_functions.h>
#include <power_signals.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define X86_NON_DSX_FORCE_SHUTDOWN_TO_MS 50

/* Power cycling primary rail requires at least 30 ms 'off' time */
#define BOARD_PTL_RVP_MINIMUM_POWER_DOWN_DELAY_MS 30

void board_ap_power_force_shutdown(void)
{
	int timeout_ms = X86_NON_DSX_FORCE_SHUTDOWN_TO_MS;

	/* Turn off PCH_RMSRST to meet tPCH12 */
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	/* Disable all control gpio here */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lcd_backoff), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_pln), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susb_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vsus_on_ec), 0);

	power_signal_set(PWR_EN_PP5000_A, 0);
	/* Wait RSMRST to be off. */
	while (power_signal_get(PWR_RSMRST_PWRGD) && (timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	};

	if (power_signal_get(PWR_RSMRST_PWRGD))
		LOG_WRN("RSMRST_PWRGD didn't go low!  Assuming G3.");

	k_msleep(BOARD_PTL_RVP_MINIMUM_POWER_DOWN_DELAY_MS);
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
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vsus_on_ec), 1);
		update_ap_boot_time(ARAIL);
	}

	/* Return 0 only if power rails have been enabled  */
	return !power_signal_get(PWR_EN_PP5000_A);
}

AP_POWER_APP_STATE_DEFINE(G3, board_ap_power_action_g3_entry,
			  board_ap_power_action_g3_run, NULL);

static int board_ap_power_action_s4_run(void *data)
{
	/* Disable SUSB_EC# before goto S5 */
	if (power_signal_get(PWR_RSMRST_PWRGD) == 0 ||
	    power_signal_get(PWR_SLP_S5)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susb_ec), 0);
		return 0;
	}

	/* Enable SUSB_EC# before goto S3 */
	if (!power_signal_get(PWR_SLP_S4)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susb_ec), 1);
		return 0;
	}

	return 0;
}

AP_POWER_APP_STATE_DEFINE(S4, NULL, board_ap_power_action_s4_run, NULL);

static int board_ap_power_action_s3_run(void *data)
{
	/* Disable VR before goto S4 */
	if (power_signal_get(PWR_RSMRST_PWRGD) == 0 ||
	    power_signal_get(PWR_SLP_S4)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lcd_backoff), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_pln), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);

		return 0;
	}

	/* Enable VR before goto S0 */
	if (!power_wait_signals_on_timeout(
		    POWER_SIGNAL_MASK(PWR_ALL_SYS_PWRGD),
		    AP_PWRSEQ_DT_VALUE(all_sys_pwrgd_timeout))) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_pln), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lcd_backoff), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), 1);

		return 0;
	}

	return 0;
}

AP_POWER_APP_STATE_DEFINE(S3, NULL, board_ap_power_action_s3_run, NULL);

int power_signal_external_init(void)
{
	return 0;
}

int board_power_signal_get(enum power_signal signal)
{
	return -EINVAL;
}

int board_power_signal_set(enum power_signal signal, int value)
{
	return -EINVAL;
}
