/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "keyboard_scan.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, vnd_keyboard_pm_action, const struct device *,
		enum pm_device_action);
FAKE_VALUE_FUNC(int, pinctrl_configure_pins, const pinctrl_soc_pin_t *, uint8_t,
		uintptr_t);
FAKE_VALUE_FUNC(int, system_is_locked);

#define VND_KEYBOARD_NODE DT_INST(0, vnd_keyboard_input_device)

PM_DEVICE_DT_DEFINE(VND_KEYBOARD_NODE, vnd_keyboard_pm_action);

DEVICE_DT_DEFINE(VND_KEYBOARD_NODE, NULL, PM_DEVICE_DT_GET(VND_KEYBOARD_NODE),
		 NULL, NULL, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		 NULL);

static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

#define FACTORY_TEST_NODE \
	DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_keyboard_factory_test)
#define P2_GPIO_NUM DT_GPIO_PIN(FACTORY_TEST_NODE, pin2_gpios)
#define P3_GPIO_NUM DT_GPIO_PIN(FACTORY_TEST_NODE, pin3_gpios)
#define P10_GPIO_NUM DT_GPIO_PIN(FACTORY_TEST_NODE, pin10_gpios)
#define P11_GPIO_NUM DT_GPIO_PIN(FACTORY_TEST_NODE, pin11_gpios)

#define COL_GPIO_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_col_gpio)
#define COL_GPIO_NUM DT_GPIO_PIN(COL_GPIO_NODE, col_gpios)

static bool simulate_short_p3_p11;

static void gpio_emul_cb_handler(const struct device *dev,
				 struct gpio_callback *gpio_cb, uint32_t pins)
{
	if (simulate_short_p3_p11 && pins == BIT(P3_GPIO_NUM)) {
		gpio_emul_input_set(gpio_dev, P11_GPIO_NUM, 0);
	}
}

static struct gpio_callback gpio_emul_cb;

ZTEST(keyboard_factory_test, test_factory_test_hc)
{
	struct ec_response_keyboard_factory_test resp;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_KEYBOARD_FACTORY_TEST, 0, resp);
	gpio_flags_t flags;

	zassert_ok(host_command_process(&args));
	zassert_equal(resp.shorted, 0);
	zassert_equal(pinctrl_configure_pins_fake.call_count, 2);

	gpio_emul_flags_get(gpio_dev, P10_GPIO_NUM, &flags);
	zassert_equal(flags, GPIO_INPUT | GPIO_PULL_UP);

	gpio_emul_flags_get(gpio_dev, COL_GPIO_NUM, &flags);
	zassert_equal(flags, GPIO_OUTPUT_LOW);
}

ZTEST(keyboard_factory_test, test_factory_test_hc_shorted)
{
	struct ec_response_keyboard_factory_test resp;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_KEYBOARD_FACTORY_TEST, 0, resp);

	simulate_short_p3_p11 = true;

	zassert_ok(host_command_process(&args));
	zassert_equal(resp.shorted, 3 << 8 | 11);
}

ZTEST(keyboard_factory_test, test_factory_test_locked)
{
	struct ec_response_keyboard_factory_test resp;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_KEYBOARD_FACTORY_TEST, 0, resp);

	system_is_locked_fake.return_val = true;

	zassert_equal(host_command_process(&args), EC_RES_ACCESS_DENIED);
	zassert_equal(resp.shorted, 0);
	zassert_equal(pinctrl_configure_pins_fake.call_count, 0);
}

ZTEST(keyboard_factory_test, test_factory_test_shell)
{
	const struct shell *shell_zephyr = shell_backend_dummy_get_ptr();
	const char *outbuffer;
	size_t buffer_size;

	/* Give the backend time to initialize */
	k_sleep(K_MSEC(100));

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "kbfactorytest"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer,
				"Keyboard factory test: shorted=0000 (0, 0)"));
}

ZTEST(keyboard_factory_test, test_factory_test_shell_shorted)
{
	const struct shell *shell_zephyr = shell_backend_dummy_get_ptr();
	const char *outbuffer;
	size_t buffer_size;

	/* Give the backend time to initialize */
	k_sleep(K_MSEC(100));

	shell_backend_dummy_clear_output(shell_zephyr);

	simulate_short_p3_p11 = true;

	zassert_ok(shell_execute_cmd(shell_zephyr, "kbfactorytest"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer,
				"Keyboard factory test: shorted=030b (3, 11)"));
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(system_is_locked);
	RESET_FAKE(vnd_keyboard_pm_action);
	RESET_FAKE(pinctrl_configure_pins);

	simulate_short_p3_p11 = false;
}

static void *keyboard_factory_test_setup(void)
{
	gpio_init_callback(&gpio_emul_cb, gpio_emul_cb_handler,
			   BIT(P2_GPIO_NUM) | BIT(P3_GPIO_NUM) |
				   BIT(P10_GPIO_NUM) | BIT(P11_GPIO_NUM));

	gpio_add_callback(gpio_dev, &gpio_emul_cb);

	return NULL;
}

ZTEST_SUITE(keyboard_factory_test, NULL, keyboard_factory_test_setup, reset,
	    reset, NULL);
