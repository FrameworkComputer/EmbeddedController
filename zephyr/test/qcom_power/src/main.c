/* Copyright 2022 The ChromiumOS Authors
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
#include "power/qcom.h"
#include "task.h"

#include <setjmp.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include <dt-bindings/buttons.h>

/* For simplicity, enforce that all the gpios are on the same controller. */
#define GPIO_DEVICE \
	DEVICE_DT_GET(DT_GPIO_CTLR(NAMED_GPIOS_GPIO_NODE(ap_rst_l), gpios))
#define ASSERT_SAME_CONTROLLER(x)                                        \
	BUILD_ASSERT(                                                    \
		DT_DEP_ORD(DT_GPIO_CTLR(NAMED_GPIOS_GPIO_NODE(ap_rst_l), \
					gpios)) ==                       \
		DT_DEP_ORD(DT_GPIO_CTLR(NAMED_GPIOS_GPIO_NODE(x), gpios)))

#define AP_RST_L_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ap_rst_l), gpios)
ASSERT_SAME_CONTROLLER(ap_rst_l);
#define POWER_GOOD_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(mb_power_good), gpios)
ASSERT_SAME_CONTROLLER(mb_power_good);
#define AP_SUSPEND_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ap_suspend), gpios)
ASSERT_SAME_CONTROLLER(ap_suspend);
#define SWITCHCAP_PG_PIN \
	DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(src_vph_pwr_pg), gpios)
ASSERT_SAME_CONTROLLER(src_vph_pwr_pg);
#define PMIC_RESIN_L_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(pmic_resin_l), gpios)
ASSERT_SAME_CONTROLLER(pmic_resin_l);
#define EC_PWR_BTN_ODL_PIN \
	DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ec_pwr_btn_odl), gpios)
ASSERT_SAME_CONTROLLER(ec_pwr_btn_odl);
#define LID_OPEN_EC_PIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(lid_open_ec), gpios)
ASSERT_SAME_CONTROLLER(lid_open_ec);
#define PMIC_KPD_PWR_ODL_PIN \
	DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(pmic_kpd_pwr_odl), gpios)
ASSERT_SAME_CONTROLLER(pmic_kpd_pwr_odl);

static int chipset_reset_count;
static bool set_power_good_on_reset;

static void do_chipset_reset(void)
{
	chipset_reset_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, do_chipset_reset, HOOK_PRIO_DEFAULT);

static void do_chipset_shutdown(void)
{
	if (set_power_good_on_reset) {
		static const struct device *gpio_dev = GPIO_DEVICE;

		gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, do_chipset_shutdown, HOOK_PRIO_DEFAULT);

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, system_can_boot_ap);
FAKE_VALUE_FUNC(int, battery_wait_for_stable);
int battery_is_present(void)
{
	return 1;
}

/* Tests the chipset_ap_rst_interrupt() handler when in S3.
 *
 * When the system is in S3, and ap_rst_l is pulsed 1-3 times then
 * HOOK_CHIPSET_RESET hooks will run, and interrupts will be disabled for
 * ap_suspend (see power_chipset_handle_host_sleep_event). This may be
 * artificial, since I'm not sure that this scenario can actually ever happen.
 */
static void do_chipset_ap_rst_interrupt_in_s3(int times)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	/* Preconditions */
	power_signal_enable_interrupt(GPIO_AP_SUSPEND);
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_SUSPEND_PIN, 1));
	power_set_state(POWER_S3);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_S3);

	shell_backend_dummy_clear_output(get_ec_shell());
	chipset_reset_count = 0;

	/* Pulse gpio_ap_rst_l `times` */
	for (int i = 0; i < times; ++i) {
		zassert_ok(gpio_emul_input_set(gpio_dev, AP_RST_L_PIN, 0));
		zassert_ok(gpio_emul_input_set(gpio_dev, AP_RST_L_PIN, 1));
	}

	/* Wait for timeout AP_RST_TRANSITION_TIMEOUT. */
	k_sleep(K_MSEC(500));

	/* Verify that gpio_ap_suspend is ignored. */
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_SUSPEND_PIN, 0));
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
	static const struct device *gpio_dev = GPIO_DEVICE;

	shell_backend_dummy_clear_output(get_ec_shell());
	chipset_reset_count = 0;

	/* Pulse gpio_ap_rst_l `times` */
	for (int i = 0; i < times; ++i) {
		zassert_ok(gpio_emul_input_set(gpio_dev, AP_RST_L_PIN, 0));
		zassert_ok(gpio_emul_input_set(gpio_dev, AP_RST_L_PIN, 1));
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
	k_sleep(K_SECONDS(10));

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
	if ((pins & BIT(PMIC_RESIN_L_PIN)) == 0) {
		return;
	}
	if (gpio_emul_output_get(gpio_dev, PMIC_RESIN_L_PIN)) {
		gpio_emul_input_set(gpio_dev, AP_RST_L_PIN, 0);
	}
}

static void set_power_good(struct k_work *work)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 1);
}
K_WORK_DEFINE(set_power_good_work, set_power_good);

/* PMIC_KPD_PWR_ODL is a signal to turn the power on. The signal that
 * it worked is POWER_GOOD.
 */
void power_good_callback(const struct device *gpio_dev,
			 struct gpio_callback *callback_struct,
			 gpio_port_pins_t pins)
{
	if ((pins & BIT(PMIC_KPD_PWR_ODL_PIN)) == 0) {
		return;
	}
	if (!gpio_emul_output_get(gpio_dev, PMIC_KPD_PWR_ODL_PIN)) {
		/* Set power good in the work queue, instead of now. */
		k_work_submit(&set_power_good_work);
	}
}

/* Call chipset_reset, wait for PMIC_RESIN_L, pulse ap_rsl_l. */
ZTEST(qcom_power, test_chipset_reset_success)
{
	static const struct device *gpio_dev = GPIO_DEVICE;
	const char *buffer;
	size_t buffer_size;

	/* Setup callback. */
	gpio_init_callback(&gpio_callback, warm_reset_callback,
			   BIT(PMIC_RESIN_L_PIN));
	zassert_ok(gpio_add_callback(gpio_dev, &gpio_callback));
	zassert_ok(gpio_pin_interrupt_configure(gpio_dev, PMIC_RESIN_L_PIN,
						GPIO_INT_EDGE_BOTH));

	/* Reset. The reason doesn't really matter. */
	shell_backend_dummy_clear_output(get_ec_shell());
	chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
	k_sleep(K_MSEC(100));
	gpio_emul_input_set(gpio_dev, AP_RST_L_PIN, 1);
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
	static const struct device *gpio_dev = GPIO_DEVICE;
	struct ec_params_host_sleep_event params = {
		.sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND,
	};

	zassert_ok(ec_cmd_host_sleep_event(NULL, &params));
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_SUSPEND_PIN, 1));
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

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(ec_cmd_host_sleep_event(NULL, &params));
	k_sleep(K_SECONDS(16));
	zassert_equal(power_get_state(), POWER_S0);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
#if defined(SECTION_IS_RW)
	zassert_true(strstr(buffer, "Detected sleep hang!") != NULL,
		     "Invalid console output %s", buffer);
	zassert_true(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));
#endif /* SECTION_IS_RW */
}

ZTEST(qcom_power, test_chipset_force_shutdown)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
	k_sleep(K_SECONDS(11));
	zassert_equal(power_get_state(), POWER_G3);
}

ZTEST(qcom_power, test_power_button)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	/* Setup callback. */
	gpio_init_callback(&gpio_callback, power_good_callback,
			   BIT(PMIC_KPD_PWR_ODL_PIN));
	zassert_ok(gpio_add_callback(gpio_dev, &gpio_callback));

	power_set_state(POWER_G3);
	zassert_ok(gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, PMIC_RESIN_L_PIN, 1));
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 0));
	k_sleep(K_MSEC(100));
	zassert_equal(power_button_signal_asserted(), 1);
	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 1));
	k_sleep(K_MSEC(500));
	zassert_equal(power_button_signal_asserted(), 0);
	zassert_equal(power_get_state(), POWER_S0);
}

#ifdef CONFIG_INPUT_GPIO_KEYS

ZTEST(qcom_power, test_power_button_input_event)
{
	const struct device *dev = DEVICE_DT_GET_ONE(gpio_keys);

	zassert_equal(power_button_is_pressed(), 0);

	input_report_key(dev, BUTTON_POWER, 1, true, K_FOREVER);
	zassert_equal(power_button_is_pressed(), 1);

	input_report_key(dev, BUTTON_RECOVERY, 1, true, K_FOREVER);
	zassert_equal(power_button_is_pressed(), 1);

	input_report_abs(dev, INPUT_ABS_X, 1, true, K_FOREVER);
	zassert_equal(power_button_is_pressed(), 1);

	input_report_key(dev, BUTTON_POWER, 0, true, K_FOREVER);
	zassert_equal(power_button_is_pressed(), 0);
}

#endif

ZTEST(qcom_power, test_power_button_no_power_good)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	zassert_ok(gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 0));
	power_set_state(POWER_G3);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 0));
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 1));
	k_sleep(K_MSEC(1500));
	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());
}

ZTEST(qcom_power, test_power_button_no_switchcap_good)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	zassert_ok(gpio_emul_input_set(gpio_dev, SWITCHCAP_PG_PIN, 0));
	power_set_state(POWER_G3);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 0));
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 1));
	k_sleep(K_SECONDS(10));
	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());
}

ZTEST(qcom_power, test_power_button_no_pmic_resin_pullup)
{
	const char *buffer;
	size_t buffer_size;
	static const struct device *gpio_dev = GPIO_DEVICE;

	power_set_state(POWER_G3);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, PMIC_RESIN_L_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 0));
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 1));
	k_sleep(K_SECONDS(10));
	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());

	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_not_null(strstr(buffer, "PMIC_RESIN_L not pulled up by PMIC"),
			 "Invalid console output %s", buffer);
}

ZTEST(qcom_power, test_power_button_battery_low)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	RESET_FAKE(system_can_boot_ap);
	system_can_boot_ap_fake.return_val = 0;

	power_set_state(POWER_G3);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 0));
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 1));
	/* > CAN_BOOT_AP_CHECK_TIMEOUT + CAN_BOOT_AP_CHECK_WAIT */
	k_sleep(K_MSEC(1800));
	zassert_equal(power_get_state(), POWER_S5);
}

ZTEST(qcom_power, test_host_sleep_event_resume)
{
	static const struct device *gpio_dev = GPIO_DEVICE;
	struct ec_params_host_sleep_event params = {
		.sleep_event = HOST_SLEEP_EVENT_S3_RESUME,
	};

	/* Get into S3 first */
	power_signal_enable_interrupt(GPIO_AP_SUSPEND);
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_SUSPEND_PIN, 1));
	power_set_state(POWER_S3);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_S3);

	/* Exit suspend via gpio. */
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_SUSPEND_PIN, 0));
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());

	/* Call host command to notify ec resume is done. */
	zassert_ok(ec_cmd_host_sleep_event(NULL, &params));
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());

	/* Check that AP_SUSPEND interrupts are disabled & we are in S0. */
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_SUSPEND_PIN, 1));
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_S0);
}

ZTEST(qcom_power, test_power_button_off)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 0));
	k_sleep(K_SECONDS(9));
	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 1));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S5);
}

ZTEST(qcom_power, test_power_button_off_cancel)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 0));
	k_sleep(K_SECONDS(4));
	zassert_ok(gpio_emul_input_set(gpio_dev, EC_PWR_BTN_ODL_PIN, 1));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0);
}

ZTEST(qcom_power, test_no_power_good)
{
	const char *buffer;
	size_t buffer_size;
	static const struct device *gpio_dev = GPIO_DEVICE;

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 0));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer, "POWER_GOOD is lost") != NULL,
		     "Invalid console output %s", buffer);
}

ZTEST(qcom_power, test_no_power_good_then_good)
{
	const char *buffer;
	size_t buffer_size;
	static const struct device *gpio_dev = GPIO_DEVICE;

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 0));
	set_power_good_on_reset = true;
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S5, "power_state=%d",
		      power_get_state());
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer, "POWER_GOOD is lost") != NULL,
		     "Invalid console output %s", buffer);
	zassert_true(strstr(buffer, "POWER_GOOD up again after lost") != NULL,
		     "Invalid console output %s", buffer);
}

ZTEST(qcom_power, test_lid_open_power_on)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	zassert_ok(gpio_emul_input_set(gpio_dev, LID_OPEN_EC_PIN, 0));
	power_set_state(POWER_G3);
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_G3);
	zassert_false(lid_is_open());

	zassert_ok(gpio_emul_input_set(gpio_dev, LID_OPEN_EC_PIN, 1));
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
}

/* chipset_power_on is called by USB code on dock power button release. */
ZTEST(qcom_power, test_chipset_power_on)
{
	power_set_state(POWER_G3);
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_G3);

	chipset_power_on();
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
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

ZTEST(qcom_power, test_power_chipset_init_sysjump_power_good)
{
	system_set_reset_flags(EC_RESET_FLAG_SYSJUMP);
	zassert_equal(power_chipset_init(), POWER_S0);
	power_set_state(POWER_S0);

	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
	zassert_equal(power_has_signals(POWER_SIGNAL_MASK(0)), 0);
}

ZTEST(qcom_power, test_power_chipset_init_sysjump_power_off)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	zassert_ok(gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 0));
	system_set_reset_flags(EC_RESET_FLAG_SYSJUMP);
	zassert_equal(power_chipset_init(), POWER_G3);
	power_set_state(POWER_G3);

	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
}

ZTEST(qcom_power, test_power_chipset_init_ap_off)
{
	system_set_reset_flags(EC_RESET_FLAG_AP_OFF);
	zassert_equal(power_chipset_init(), POWER_G3);
	power_set_state(POWER_G3);

	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
}

void start_in_s0(void *fixture)
{
	static const struct device *gpio_dev = GPIO_DEVICE;

	RESET_FAKE(system_can_boot_ap);
	system_can_boot_ap_fake.return_val = 1;
	set_power_good_on_reset = false;

	power_signal_disable_interrupt(GPIO_AP_SUSPEND);
	power_signal_enable_interrupt(GPIO_AP_RST_L);
	zassert_ok(gpio_emul_input_set(gpio_dev, POWER_GOOD_PIN, 1));
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_SUSPEND_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, AP_RST_L_PIN, 1));
	zassert_ok(gpio_emul_input_set(gpio_dev, SWITCHCAP_PG_PIN, 1));
	zassert_ok(gpio_pin_set(gpio_dev, PMIC_RESIN_L_PIN, 1));
	zassert_ok(gpio_emul_input_set(gpio_dev, LID_OPEN_EC_PIN, 1));
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
		static const struct device *gpio_dev = GPIO_DEVICE;

		gpio_remove_callback(gpio_dev, &gpio_callback);
		gpio_callback.handler = NULL;
	}
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT));
	system_clear_reset_flags(EC_RESET_FLAG_SYSJUMP | EC_RESET_FLAG_AP_OFF);
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
