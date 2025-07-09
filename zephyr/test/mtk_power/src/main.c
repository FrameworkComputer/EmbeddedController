/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_app_main.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "host_command.h"
#include "include/power_button.h"
#include "lid_switch.h"
#include "power.h"
#include "power/mt8186.h"
#include "task.h"

#include <setjmp.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include <dt-bindings/buttons.h>

LOG_MODULE_REGISTER(mtk_power, LOG_LEVEL_INF);

/* For simplicity, enforce that all the gpios are on the same controller. */
#define GPIO_DEVICE    \
	DEVICE_DT_GET( \
		DT_GPIO_CTLR(NAMED_GPIOS_GPIO_NODE(ap_ec_sysrst_odl), gpios))
#define ASSERT_SAME_CONTROLLER(x)                                           \
	BUILD_ASSERT(                                                       \
		DT_DEP_ORD(DT_GPIO_CTLR(                                    \
			NAMED_GPIOS_GPIO_NODE(ap_ec_sysrst_odl), gpios)) == \
		DT_DEP_ORD(DT_GPIO_CTLR(NAMED_GPIOS_GPIO_NODE(x), gpios)))

#define AP_IN_RST DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ap_ec_sysrst_odl), gpios)
ASSERT_SAME_CONTROLLER(ap_ec_sysrst_odl);
#define EC_AP_RST_L DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ec_ap_sys_rst_odl), gpios)
ASSERT_SAME_CONTROLLER(ec_ap_sys_rst_odl);
#define AP_IN_SLEEP DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ap_in_sleep_l), gpios)
ASSERT_SAME_CONTROLLER(ap_in_sleep_l);
#define AP_WARM_RST_REQ \
	DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ap_ec_warm_rst_req), gpios)
ASSERT_SAME_CONTROLLER(ap_ec_warm_rst_req);
#define AP_WDGT_RST_REQ \
	DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ap_ec_wdtrst_l), gpios)
ASSERT_SAME_CONTROLLER(ap_ec_wdtrst_l);
#define PG_PP3700_S5 DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(pg_pp3700_s5_od), gpios)
ASSERT_SAME_CONTROLLER(pg_pp3700_s5_od);
#define LID_OPEN_EC_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(lid_open_ec), gpios)
ASSERT_SAME_CONTROLLER(lid_open_ec);

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, system_can_boot_ap);
FAKE_VALUE_FUNC(int, battery_wait_for_stable);

static struct gpio_callback gpio_callback;

int battery_is_present(void)
{
	return 1;
}

void mtk_set_power_state(enum power_state target)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	if (target == POWER_S3) {
		/* preconditions */
		power_signal_enable_interrupt(GPIO_AP_IN_SLEEP_L);
		zassert_ok(gpio_emul_input_set(gpio_dev, AP_IN_SLEEP, 1));
		task_wake(TASK_ID_CHIPSET);
		/* wait for S3 processing */
		k_sleep(K_MSEC(10));
	} else {
		/* unsupported power state */
		zassert_ok(false);
		return;
	}
	zassert_equal(power_get_state(), target);
}

ZTEST(mtk_power, test_watchdog_reset_in_s3)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	mtk_set_power_state(POWER_S3);

	/* Trigger AP watchdog. */
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_WDGT_RST_REQ, 1));
	k_sleep(K_MSEC(1));
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_WDGT_RST_REQ, 0));

	/* Wait 150+ms for debouncing fake AP watchdog interrupt */
	k_sleep(K_MSEC(160));
	zassert_equal(chipset_get_shutdown_reason(), CHIPSET_RESET_AP_WATCHDOG);
}

void start_in_s0(void *fixture)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	RESET_FAKE(system_can_boot_ap);
	system_can_boot_ap_fake.return_val = 1;

	power_signal_enable_interrupt(GPIO_AP_EC_SYSRST_ODL);
	power_signal_enable_interrupt(GPIO_AP_IN_SLEEP_L);
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_IN_RST, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_IN_SLEEP, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, PG_PP3700_S5, 1));
	zassert_ok(gpio_pin_set(gpio_dev, EC_AP_RST_L, 1));
	zassert_ok(gpio_emul_input_set(gpio_dev, LID_OPEN_EC_PIN, 1));
	power_signal_interrupt(GPIO_AP_IN_SLEEP_L);

	task_wake(TASK_ID_CHIPSET);
	/* Wait for power state transition. */
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
	zassert_equal(power_has_signals(POWER_SIGNAL_MASK(0)), 0);
}

void mtk_cleanup(void *fixture)
{
	if (gpio_callback.handler != NULL) {
		static const struct device *gpio_dev = GPIO_DEVICE;

		gpio_remove_callback(gpio_dev, &gpio_callback);
		gpio_callback.handler = NULL;
	}
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT));
	system_clear_reset_flags(EC_RESET_FLAG_SYSJUMP | EC_RESET_FLAG_AP_OFF);
}

ZTEST_SUITE(mtk_power, NULL, NULL, start_in_s0, mtk_cleanup, NULL);

void test_main(void)
{
	ec_app_main();
	/* Fake sleep long enough to go to S5 and back to G3 again. */
	k_sleep(K_SECONDS(11));

	ztest_run_test_suites(NULL, false, 1, 1);

	ztest_verify_all_test_suites_ran();
}
