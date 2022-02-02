/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <drivers/emul.h>
#include <drivers/gpio.h>
#include <drivers/gpio/gpio_emul.h>
#include <lid_switch.h>
#include <shell/shell_dummy.h>
#include <console.h>

#include "test_state.h"
#include "ec_commands.h"
#include "host_command.h"

#define LID_GPIO_PATH DT_PATH(named_gpios, lid_open_ec)
#define LID_GPIO_PIN DT_GPIO_PIN(LID_GPIO_PATH, gpios)

int emul_lid_open(void)
{
	const struct device *lid_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(LID_GPIO_PATH, gpios));

	return gpio_emul_input_set(lid_gpio_dev, LID_GPIO_PIN, 1);
}

int emul_lid_close(void)
{
	const struct device *lid_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(LID_GPIO_PATH, gpios));

	return gpio_emul_input_set(lid_gpio_dev, LID_GPIO_PIN, 0);
}

static void cleanup(void *unused)
{
	struct ec_params_force_lid_open params = {
		.enabled = 0,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_FORCE_LID_OPEN, 0, params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);

	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(100));
}

ZTEST(lid_switch, test_lid_open)
{
	/* Start closed. */
	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 0, NULL);

	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 1, NULL);
}

ZTEST(lid_switch, test_lid_debounce)
{
	/* Start closed. */
	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 0, NULL);

	/* Create interrupts quickly before they can be handled. */
	zassert_ok(emul_lid_open(), NULL);
	zassert_ok(emul_lid_close(), NULL);
	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 1, NULL);
}

ZTEST(lid_switch, test_lid_close)
{
	/* Start open. */
	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(100));

	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(200));
	zassert_equal(lid_is_open(), 0, NULL);
}

ZTEST(lid_switch, test_cmd_lidopen)
{
	/* Start closed. */
	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 0, NULL);

	/* Forced override lid open. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "lidopen"),
		      NULL);
	zassert_equal(lid_is_open(), 1, NULL);
	k_sleep(K_MSEC(100));

	printk("GPIO lid open/close\n");
	/* Open & close with gpio. */
	zassert_ok(emul_lid_open(), NULL);
	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(500));
	zassert_equal(lid_is_open(), 0, NULL);
}

ZTEST(lid_switch, test_cmd_lidopen_bounce)
{
	/* Start closed. */
	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 0, NULL);

	printk("Console lid open\n");
	/* Forced override lid open. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "lidopen"),
		      NULL);
	zassert_equal(lid_is_open(), 1, NULL);
	k_sleep(K_MSEC(100));

	printk("Console lid open\n");
	/* Forced override lid open. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "lidopen"),
		      NULL);
	zassert_equal(lid_is_open(), 1, NULL);
	k_sleep(K_MSEC(100));

	printk("GPIO lid open/close\n");
	/* Open & close with gpio. */
	zassert_ok(emul_lid_open(), NULL);
	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(500));
	zassert_equal(lid_is_open(), 0, NULL);
}

ZTEST(lid_switch, test_cmd_lidclose)
{
	/* Start open. */
	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 1, NULL);

	/* Forced override lid close. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "lidclose"),
		      NULL);
	zassert_equal(lid_is_open(), 0, NULL);
	k_sleep(K_MSEC(100));

	printk("GPIO lid close/open\n");
	/* Close & open with gpio. */
	zassert_ok(emul_lid_close(), NULL);
	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(500));
	zassert_equal(lid_is_open(), 1, NULL);
}

ZTEST(lid_switch, test_cmd_lidclose_bounce)
{
	/* Start open. */
	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 1, NULL);

	/* Forced override lid close. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "lidclose"),
		      NULL);
	zassert_equal(lid_is_open(), 0, NULL);
	k_sleep(K_MSEC(100));

	/* Forced override lid close. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "lidclose"),
		      NULL);
	zassert_equal(lid_is_open(), 0, NULL);
	k_sleep(K_MSEC(100));

	printk("GPIO lid close/open\n");
	/* Close & open with gpio. */
	zassert_ok(emul_lid_close(), NULL);
	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(500));
	zassert_equal(lid_is_open(), 1, NULL);
}

#if defined(CONFIG_SHELL_BACKEND_DUMMY)
ZTEST(lid_switch, test_cmd_lidstate_open)
{
	const char *buffer;
	size_t buffer_size;

	/* Start open. */
	zassert_ok(emul_lid_open(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 1, NULL);

	/* Read the state with console. */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "lidstate"),
		      NULL);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strcmp(buffer, "\r\nlid state: open\r\n") == 0,
		     "Invalid console output %s", buffer);
}

ZTEST(lid_switch, test_cmd_lidstate_close)
{
	const char *buffer;
	size_t buffer_size;

	/* Start closed. */
	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 0, NULL);

	/* Read the state with console. */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "lidstate"),
		      NULL);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strcmp(buffer, "\r\nlid state: closed\r\n") == 0,
		     "Invalid console output %s", buffer);
}
#else
#error This test requires CONFIG_SHELL_BACKEND_DUMMY
#endif

ZTEST(lid_switch, test_hc_force_lid_open)
{
	struct ec_params_force_lid_open params = {
		.enabled = 1,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_FORCE_LID_OPEN, 0, params);

	/* Start closed. */
	zassert_ok(emul_lid_close(), NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 0, NULL);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	k_sleep(K_MSEC(100));
	zassert_equal(lid_is_open(), 1, NULL);
}

ZTEST_SUITE(lid_switch, drivers_predicate_post_main, NULL, NULL, &cleanup,
	    NULL);
