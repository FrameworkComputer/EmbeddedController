/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_events.h"
#include "base_state.h"
#include "chipset.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "tablet_mode.h"
#include "task.h"

#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define ATTACH_MAX_THRESHOLD_MV 300
#define UNATTACH_THRESHOLD_MV 1800

extern bool is_held;

static void set_signal_state(enum power_state state)
{
	const struct gpio_dt_spec *ap_ec_sysrst_odl =
		gpio_get_dt_spec(GPIO_AP_EC_SYSRST_ODL);
	const struct gpio_dt_spec *ap_in_sleep_l =
		gpio_get_dt_spec(GPIO_AP_IN_SLEEP_L);

	if (state == POWER_S0) {
		gpio_emul_input_set(ap_in_sleep_l->port, ap_in_sleep_l->pin, 1);
		gpio_emul_input_set(ap_ec_sysrst_odl->port,
				    ap_ec_sysrst_odl->pin, 1);
	}

	/* reset is_held flag */
	is_held = false;
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(1));
}

static void *base_detect_setup(void)
{
	/* G3 -> S0 */
	power_set_state(POWER_G3);
	set_signal_state(POWER_S0);

	return NULL;
}

ZTEST_SUITE(baes_detect, NULL, base_detect_setup, NULL, NULL, NULL);

ZTEST(baes_detect, test_base_detect_startup)
{
	const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
	const uint8_t adc_channel =
		DT_IO_CHANNELS_INPUT(DT_NODELABEL(adc_base_det));

	/* Verify power on when keyboard is plugged in or out */
	hook_notify(HOOK_INIT);
	zassert_ok(adc_emul_const_value_set(adc_dev, adc_channel,
					    ATTACH_MAX_THRESHOLD_MV));
	k_sleep(K_MSEC(1000));

	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_equal(0, tablet_get_mode(), NULL);

	zassert_ok(adc_emul_const_value_set(adc_dev, adc_channel,
					    UNATTACH_THRESHOLD_MV));
	k_sleep(K_MSEC(1000));
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	k_sleep(K_MSEC(100));
	zassert_equal(1, tablet_get_mode());
}

ZTEST(baes_detect, test_base_detect_shutdown)
{
	/* Verify shutdown when keyboard is plugged in or out */
	hook_notify(HOOK_INIT);
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_equal(1, tablet_get_mode(), NULL);
}

ZTEST(baes_detect, test_base_detect_interrupt)
{
	const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
	const uint8_t adc_channel =
		DT_IO_CHANNELS_INPUT(DT_NODELABEL(adc_base_det));

	/*
	 * Verify that an interrupt is triggered when the keyboard is
	 * inserted or removed
	 */
	hook_notify(HOOK_INIT);
	zassert_ok(adc_emul_const_value_set(adc_dev, adc_channel,
					    ATTACH_MAX_THRESHOLD_MV));
	k_sleep(K_MSEC(500));
	zassert_equal(0, tablet_get_mode(), NULL);

	zassert_ok(adc_emul_const_value_set(adc_dev, adc_channel,
					    UNATTACH_THRESHOLD_MV));
	k_sleep(K_MSEC(1000));
	zassert_equal(1, tablet_get_mode(), NULL);
}

ZTEST_SUITE(console_cmd_setbasestate, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(console_cmd_setbasestate, test_sb_setbasestate)
{
	int rv;

	const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
	const uint8_t adc_channel =
		DT_IO_CHANNELS_INPUT(DT_NODELABEL(adc_base_det));

	/* command to basestate attach*/
	rv = shell_execute_cmd(get_ec_shell(), "basestate attach");
	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);
	k_sleep(K_MSEC(500));
	zassert_equal(0, tablet_get_mode(), NULL);

	/* command to basestate detach*/
	rv = shell_execute_cmd(get_ec_shell(), "basestate detach");
	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);
	k_sleep(K_MSEC(500));
	zassert_equal(1, tablet_get_mode(), NULL);

	/* command to basestate resaet*/
	zassert_ok(adc_emul_const_value_set(adc_dev, adc_channel,
					    ATTACH_MAX_THRESHOLD_MV));
	rv = shell_execute_cmd(get_ec_shell(), "basestate reset");
	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);
	k_sleep(K_MSEC(1000));
	zassert_equal(0, tablet_get_mode(), NULL);

	zassert_ok(adc_emul_const_value_set(adc_dev, adc_channel,
					    UNATTACH_THRESHOLD_MV));
	rv = shell_execute_cmd(get_ec_shell(), "basestate reset");
	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);
	k_sleep(K_MSEC(1000));
	zassert_equal(1, tablet_get_mode(), NULL);
}
