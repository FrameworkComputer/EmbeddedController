/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/fff.h>

#include <setjmp.h>

#include "gpio_signal.h"
#include "power/qcom.h"
#include "battery.h"
#include "ec_app_main.h"
#include "power.h"
#include "console.h"
#include "task.h"
#include "hooks.h"
#include "host_command.h"

#define AP_RST_L_NODE DT_PATH(named_gpios, ap_rst_l)
#define POWER_GOOD_NODE DT_PATH(named_gpios, mb_power_good)
#define AP_SUSPEND_NODE DT_PATH(named_gpios, ap_suspend)
#define SWITCHCAP_PG_NODE DT_PATH(named_gpios, switchcap_pg_int_l)
#define PMIC_RESIN_L_NODE DT_PATH(named_gpios, pmic_resin_l)
#define EC_PWR_BTN_ODL_NODE DT_PATH(named_gpios, ec_pwr_btn_odl)

static int chipset_reset_count;

static void do_chipset_reset(void)
{
	chipset_reset_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, do_chipset_reset, HOOK_PRIO_DEFAULT);

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, system_can_boot_ap);
FAKE_VALUE_FUNC(int, battery_wait_for_stable);

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
	static const struct device *ap_suspend_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_SUSPEND_NODE, gpios));

	/* Preconditions */
	power_signal_enable_interrupt(GPIO_AP_SUSPEND);
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
	const char *buffer;
	size_t buffer_size;

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

static struct gpio_callback gpio_callback;

/* warm_reset_seq pulses PMIC_RESIN_L, at the end of that pulse set AP_RST_L. */
void warm_reset_callback(const struct device *gpio_dev,
			 struct gpio_callback *callback_struct,
			 gpio_port_pins_t pins)
{
	if ((pins & BIT(DT_GPIO_PIN(PMIC_RESIN_L_NODE, gpios))) == 0) {
		return;
	}
	if (gpio_emul_output_get(gpio_dev,
				 DT_GPIO_PIN(PMIC_RESIN_L_NODE, gpios))) {
		static const struct device *ap_rst_dev =
			DEVICE_DT_GET(DT_GPIO_CTLR(AP_RST_L_NODE, gpios));

		gpio_emul_input_set(ap_rst_dev,
				    DT_GPIO_PIN(AP_RST_L_NODE, gpios), 0);
	}
}

/* Call chipset_reset, wait for PMIC_RESIN_L, pulse ap_rsl_l. */
ZTEST(qcom_power, test_chipset_reset_success)
{
	static const struct device *ap_rst_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_RST_L_NODE, gpios));
	static const struct device *pmic_resin_l_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(PMIC_RESIN_L_NODE, gpios));
	const char *buffer;
	size_t buffer_size;

	/* Setup callback. */
	gpio_init_callback(&gpio_callback, warm_reset_callback,
			   BIT(DT_GPIO_PIN(PMIC_RESIN_L_NODE, gpios)));
	zassert_ok(gpio_add_callback(pmic_resin_l_dev, &gpio_callback));

	/* Reset. The reason doesn't really matter. */
	shell_backend_dummy_clear_output(get_ec_shell());
	chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
	k_sleep(K_MSEC(100));
	gpio_emul_input_set(ap_rst_dev, DT_GPIO_PIN(AP_RST_L_NODE, gpios), 1);
	/* Long enough for a cold reset, although we don't expect one. */
	k_sleep(K_MSEC(1000));

	/* Verify logged messages. */
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_false(strstr(buffer,
			     "AP refuses to warm reset. Cold resetting") !=
			      NULL,
		      "Invalid console output %s", buffer);
	zassert_false(strstr(buffer, "power state 1 = S5") != NULL,
		      "Invalid console output %s", buffer);
	zassert_equal(power_get_state(), POWER_S0);
}

/* Sent the host command, set the gpio, wait for transition to S3. */
ZTEST(qcom_power, test_request_sleep)
{
	static const struct device *ap_suspend_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_SUSPEND_NODE, gpios));
	struct ec_params_host_sleep_event params = {
		.sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_HOST_SLEEP_EVENT, UINT8_C(0), params);

	zassert_ok(host_command_process(&args));
	zassert_ok(gpio_emul_input_set(ap_suspend_dev,
				       DT_GPIO_PIN(AP_SUSPEND_NODE, gpios), 1));
	k_sleep(K_SECONDS(16));
	zassert_equal(power_get_state(), POWER_S3);
	zassert_false(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));
}

/* Sent the host command, don't set the gpio, look for host event. */
ZTEST(qcom_power, test_request_sleep_timeout)
{
	const char *buffer;
	size_t buffer_size;
	struct ec_params_host_sleep_event params = {
		.sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_HOST_SLEEP_EVENT, UINT8_C(0), params);

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(host_command_process(&args));
	k_sleep(K_SECONDS(16));
	zassert_equal(power_get_state(), POWER_S0);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer, "Detected sleep hang!") != NULL,
		     "Invalid console output %s", buffer);
	zassert_true(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));
}

ZTEST(qcom_power, test_chipset_force_shutdown)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
	k_sleep(K_SECONDS(11));
	zassert_equal(power_get_state(), POWER_G3);
}

ZTEST(qcom_power, test_power_button)
{
	static const struct device *ec_pwr_btn_odl_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(EC_PWR_BTN_ODL_NODE, gpios));

	power_set_state(POWER_G3);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	zassert_ok(gpio_emul_input_set(ec_pwr_btn_odl_dev,
				       DT_GPIO_PIN(EC_PWR_BTN_ODL_NODE, gpios),
				       0));
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(ec_pwr_btn_odl_dev,
				       DT_GPIO_PIN(EC_PWR_BTN_ODL_NODE, gpios),
				       1));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0);
}

ZTEST(qcom_power, test_power_button_no_power_good)
{
	static const struct device *ec_pwr_btn_odl_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(EC_PWR_BTN_ODL_NODE, gpios));
	static const struct device *power_good_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(POWER_GOOD_NODE, gpios));

	zassert_ok(gpio_emul_input_set(power_good_dev,
				       DT_GPIO_PIN(POWER_GOOD_NODE, gpios), 0));
	power_set_state(POWER_G3);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	zassert_ok(gpio_emul_input_set(ec_pwr_btn_odl_dev,
				       DT_GPIO_PIN(EC_PWR_BTN_ODL_NODE, gpios),
				       0));
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(ec_pwr_btn_odl_dev,
				       DT_GPIO_PIN(EC_PWR_BTN_ODL_NODE, gpios),
				       1));
	k_sleep(K_MSEC(900));
	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());
}

ZTEST(qcom_power, test_power_button_battery_low)
{
	static const struct device *ec_pwr_btn_odl_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(EC_PWR_BTN_ODL_NODE, gpios));

	RESET_FAKE(system_can_boot_ap);
	system_can_boot_ap_fake.return_val = 0;

	power_set_state(POWER_G3);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	zassert_ok(gpio_emul_input_set(ec_pwr_btn_odl_dev,
				       DT_GPIO_PIN(EC_PWR_BTN_ODL_NODE, gpios),
				       0));
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(ec_pwr_btn_odl_dev,
				       DT_GPIO_PIN(EC_PWR_BTN_ODL_NODE, gpios),
				       1));
	/* > CAN_BOOT_AP_CHECK_TIMEOUT + CAN_BOOT_AP_CHECK_WAIT */
	k_sleep(K_MSEC(1800));
	zassert_equal(power_get_state(), POWER_S5);
}

ZTEST(qcom_power, test_host_sleep_event_resume)
{
	static const struct device *ap_suspend_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_SUSPEND_NODE, gpios));
	struct ec_params_host_sleep_event params = {
		.sleep_event = HOST_SLEEP_EVENT_S3_RESUME,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_HOST_SLEEP_EVENT, UINT8_C(0), params);

	/* Get into S3 first */
	power_signal_enable_interrupt(GPIO_AP_SUSPEND);
	zassert_ok(gpio_emul_input_set(ap_suspend_dev,
				       DT_GPIO_PIN(AP_SUSPEND_NODE, gpios), 1));
	power_set_state(POWER_S3);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_S3);

	/* Exit suspend via gpio. */
	zassert_ok(gpio_emul_input_set(ap_suspend_dev,
				       DT_GPIO_PIN(AP_SUSPEND_NODE, gpios), 0));
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());

	/* Call host command to notify ec resume is done. */
	zassert_ok(host_command_process(&args));
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());

	/* Check that AP_SUSPEND interrupts are disabled & we are in S0. */
	zassert_ok(gpio_emul_input_set(ap_suspend_dev,
				       DT_GPIO_PIN(AP_SUSPEND_NODE, gpios), 1));
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_S0);
}

static jmp_buf assert_jumpdata;
static int num_asserts;

void assert_post_action(const char *file, unsigned int line)
{
	++num_asserts;
	longjmp(assert_jumpdata, 1);
}

ZTEST(qcom_power, test_invalid_power_state)
{
	if (setjmp(assert_jumpdata) == 0) {
		power_handle_state(POWER_S4);
		zassert_unreachable();
	}
	zassert_equal(num_asserts, 1);
}

void start_in_s0(void *fixture)
{
	static const struct device *ap_rst_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_RST_L_NODE, gpios));
	static const struct device *power_good_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(POWER_GOOD_NODE, gpios));
	static const struct device *ap_suspend_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(AP_SUSPEND_NODE, gpios));
	static const struct device *switchcap_pg_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(SWITCHCAP_PG_NODE, gpios));
	static const struct device *pmic_resin_l_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(PMIC_RESIN_L_NODE, gpios));

	RESET_FAKE(system_can_boot_ap);
	system_can_boot_ap_fake.return_val = 1;

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
	zassert_ok(gpio_pin_set(pmic_resin_l_dev,
				DT_GPIO_PIN(PMIC_RESIN_L_NODE, gpios), 1));
	power_set_state(POWER_S0);
	power_signal_interrupt(GPIO_AP_SUSPEND);
	task_wake(TASK_ID_CHIPSET);
	/* Wait for timeout AP_RST_TRANSITION_TIMEOUT. */
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
	zassert_equal(power_has_signals(POWER_SIGNAL_MASK(0)), 0);
}

void qcom_cleanup(void *fixture)
{
	if (gpio_callback.handler != NULL) {
		static const struct device *pmic_resin_l_dev =
			DEVICE_DT_GET(DT_GPIO_CTLR(PMIC_RESIN_L_NODE, gpios));
		gpio_remove_callback(pmic_resin_l_dev, &gpio_callback);
		gpio_callback.handler = NULL;
	}
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT));
}

ZTEST_SUITE(qcom_power, NULL, NULL, start_in_s0, qcom_cleanup, NULL);

void test_main(void)
{
	ec_app_main();
	/* Fake sleep long enough to go to S5 and back to G3 again. */
	k_sleep(K_SECONDS(11));

	ztest_run_test_suites(NULL);

	ztest_verify_all_test_suites_ran();
}
