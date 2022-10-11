/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/shell/shell_dummy.h>

#include "gpio_signal.h"
#include "power/qcom.h"
#include "battery.h"
#include "ec_app_main.h"
#include "power.h"
#include "console.h"
#include "task.h"
#include "hooks.h"

#define AP_RST_L_NODE DT_PATH(named_gpios, ap_rst_l)
#define POWER_GOOD_NODE DT_PATH(named_gpios, mb_power_good)
#define AP_SUSPEND_NODE DT_PATH(named_gpios, ap_suspend)
#define SWITCHCAP_PG_NODE DT_PATH(named_gpios, switchcap_pg_int_l)

static int chipset_reset_count;

static void do_chipset_reset(void)
{
	chipset_reset_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, do_chipset_reset, HOOK_PRIO_DEFAULT);

/* Tests the chipset_ap_rst_interrupt() handler when in S3.
 *
 * When the system is in S3, and ap_rst_l is pulsed 1-3 times then
 * HOOK_CHIPSET_RESET hooks will run, and interrupts will be disabled for
 * ap_suspend (see power_chipset_handle_host_sleep_event). This may be
 * artificial, since I'm not sure that this scenario can actually ever happen.
 */
static void do_chipset_ap_rst_interrupt_in_s3(int times)
{
	static const struct device *ap_rst_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_RST_L_NODE, gpios));
	static const struct device *power_good_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(POWER_GOOD_NODE, gpios));
	static const struct device *ap_suspend_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_SUSPEND_NODE, gpios));

	/* Preconditions */
	power_signal_enable_interrupt(GPIO_AP_SUSPEND);
	power_signal_enable_interrupt(GPIO_AP_RST_L);
	zassert_ok(gpio_emul_input_set(power_good_dev,
				       DT_GPIO_PIN(POWER_GOOD_NODE, gpios), 1));
	zassert_ok(gpio_emul_input_set(ap_suspend_dev,
				       DT_GPIO_PIN(AP_SUSPEND_NODE, gpios), 1));
	power_set_state(POWER_S3);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_S3);

	shell_backend_dummy_clear_output(get_ec_shell());
	chipset_reset_count = 0;

	/* Pulse gpio_ap_rst_l `times` */
	for (int i = 0; i < times; ++i) {
		zassert_ok(gpio_emul_input_set(
			ap_rst_dev, DT_GPIO_PIN(AP_RST_L_NODE, gpios), 0));
		zassert_ok(gpio_emul_input_set(
			ap_rst_dev, DT_GPIO_PIN(AP_RST_L_NODE, gpios), 1));
	}

	/* Wait for timeout AP_RST_TRANSITION_TIMEOUT. */
	k_sleep(K_MSEC(500));

	/* Verify that gpio_ap_suspend is ignored. */
	zassert_ok(gpio_emul_input_set(ap_suspend_dev,
				       DT_GPIO_PIN(AP_SUSPEND_NODE, gpios), 0));
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_S3);
	/* Verify that HOOK_CHIPSET_RESET was called once. */
	zassert_equal(chipset_reset_count, 1);
}

ZTEST(qcom_power, test_notify_chipset_reset_s3_timeout)
{
	const char *buffer;
	size_t buffer_size;

	do_chipset_ap_rst_interrupt_in_s3(1);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer, "AP_RST_L transitions not expected: 1") !=
			     NULL,
		     "Invalid console output %s", buffer);
	zassert_true(strstr(buffer, "Chipset reset: exit s3") != NULL,
		     "Invalid console output %s", buffer);
}

ZTEST(qcom_power, test_notify_chipset_reset_s3)
{
	const char *buffer;
	size_t buffer_size;

	do_chipset_ap_rst_interrupt_in_s3(3);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_false(strstr(buffer, "AP_RST_L transitions not expected") !=
			      NULL,
		      "Invalid console output %s", buffer);
	zassert_true(strstr(buffer, "Chipset reset: exit s3") != NULL,
		     "Invalid console output %s", buffer);
}

/* Tests the chipset_ap_rst_interrupt() handler when in S0.
 *
 * When the system is in S0, and ap_rst_l is pulsed 1-3 times then
 * HOOK_CHIPSET_RESET hooks will run, and that is pretty much all that happens.
 */
static void do_chipset_ap_rst_interrupt_in_s0(int times)
{
	static const struct device *ap_rst_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_RST_L_NODE, gpios));
	static const struct device *power_good_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(POWER_GOOD_NODE, gpios));
	static const struct device *ap_suspend_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_SUSPEND_NODE, gpios));

	/* Preconditions */
	zassert_ok(gpio_emul_input_set(power_good_dev,
				       DT_GPIO_PIN(POWER_GOOD_NODE, gpios), 1));
	zassert_ok(gpio_emul_input_set(ap_suspend_dev,
				       DT_GPIO_PIN(AP_SUSPEND_NODE, gpios), 0));
	power_set_state(POWER_S0);
	power_signal_disable_interrupt(GPIO_AP_SUSPEND);
	power_signal_enable_interrupt(GPIO_AP_RST_L);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_S0);

	shell_backend_dummy_clear_output(get_ec_shell());
	chipset_reset_count = 0;

	/* Pulse gpio_ap_rst_l `times` */
	for (int i = 0; i < times; ++i) {
		zassert_ok(gpio_emul_input_set(
			ap_rst_dev, DT_GPIO_PIN(AP_RST_L_NODE, gpios), 0));
		zassert_ok(gpio_emul_input_set(
			ap_rst_dev, DT_GPIO_PIN(AP_RST_L_NODE, gpios), 1));
	}

	/* Wait for timeout AP_RST_TRANSITION_TIMEOUT. */
	k_sleep(K_MSEC(500));

	/* Verify that HOOK_CHIPSET_RESET was called once. */
	zassert_equal(chipset_reset_count, 1);
}

ZTEST(qcom_power, test_notify_chipset_reset_s0_timeout)
{
	const char *buffer;
	size_t buffer_size;

	do_chipset_ap_rst_interrupt_in_s0(1);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer, "AP_RST_L transitions not expected: 1") !=
			     NULL,
		     "Invalid console output %s", buffer);
	zassert_false(strstr(buffer, "Chipset reset: exit s3") != NULL,
		      "Invalid console output %s", buffer);
}

ZTEST(qcom_power, test_notify_chipset_reset_s0)
{
	const char *buffer;
	size_t buffer_size;

	do_chipset_ap_rst_interrupt_in_s0(3);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_false(strstr(buffer, "AP_RST_L transitions not expected") !=
			      NULL,
		      "Invalid console output %s", buffer);
	zassert_false(strstr(buffer, "Chipset reset: exit s3") != NULL,
		      "Invalid console output %s", buffer);
}

/* Call chipset_reset, don't provide signals from AP. Verify logs. */
ZTEST(qcom_power, test_chipset_reset_timeout)
{
	static const struct device *ap_rst_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_RST_L_NODE, gpios));
	static const struct device *power_good_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(POWER_GOOD_NODE, gpios));
	static const struct device *ap_suspend_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_SUSPEND_NODE, gpios));
	static const struct device *switchcap_pg_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(SWITCHCAP_PG_NODE, gpios));
	const char *buffer;
	size_t buffer_size;

	/* Preconditions */
	power_signal_disable_interrupt(GPIO_AP_SUSPEND);
	power_signal_enable_interrupt(GPIO_AP_RST_L);
	zassert_ok(gpio_emul_input_set(power_good_dev,
				       DT_GPIO_PIN(POWER_GOOD_NODE, gpios), 1));
	zassert_ok(gpio_emul_input_set(ap_suspend_dev,
				       DT_GPIO_PIN(AP_SUSPEND_NODE, gpios), 0));
	zassert_ok(gpio_emul_input_set(ap_rst_dev,
				       DT_GPIO_PIN(AP_RST_L_NODE, gpios), 1));
	zassert_ok(gpio_emul_input_set(
		switchcap_pg_dev, DT_GPIO_PIN(SWITCHCAP_PG_NODE, gpios), 1));
	power_set_state(POWER_S0);
	task_wake(TASK_ID_CHIPSET);
	/* Wait for timeout AP_RST_TRANSITION_TIMEOUT. */
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0);
	zassert_equal(power_has_signals(POWER_SIGNAL_MASK(0)), 0);

	/* Reset. The reason doesn't really matter. */
	shell_backend_dummy_clear_output(get_ec_shell());
	chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
	/* Long enough for the cold reset. */
	k_sleep(K_MSEC(1000));

	/* Verify logged messages. */
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer,
			    "AP refuses to warm reset. Cold resetting") != NULL,
		     "Invalid console output %s", buffer);
	zassert_true(strstr(buffer, "power state 1 = S5") != NULL,
		     "Invalid console output %s", buffer);
	zassert_equal(power_get_state(), POWER_S0);
}

ZTEST_SUITE(qcom_power, NULL, NULL, NULL, NULL, NULL);

/* Wait until battery is totally stable */
int battery_wait_for_stable(void)
{
	return EC_SUCCESS;
}

void test_main(void)
{
	ec_app_main();
	/* Fake sleep long enough to go to S5 and back to G3 again. */
	k_sleep(K_SECONDS(11));

	ztest_run_test_suites(NULL);

	ztest_verify_all_test_suites_ran();
}
