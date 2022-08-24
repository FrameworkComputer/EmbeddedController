/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "emul/emul_flash.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#define WP_L_GPIO_PATH DT_PATH(named_gpios, wp_l)

static int gpio_wp_l_set(int value)
{
	const struct device *wp_l_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(WP_L_GPIO_PATH, gpios));

	return gpio_emul_input_set(wp_l_gpio_dev,
				   DT_GPIO_PIN(WP_L_GPIO_PATH, gpios), value);
}

ZTEST_USER(flash, test_hostcmd_flash_protect_wp_asserted)
{
	struct ec_response_flash_protect response;
	struct ec_params_flash_protect params = {
		.mask = 0,
		.flags = 0,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_FLASH_PROTECT, 0, response, params);
	/* The original flags not 0 as GPIO WP_L asserted */
	uint32_t expected_flags = EC_FLASH_PROTECT_GPIO_ASSERTED;

	/* Get the flash protect */
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable RO_AT_BOOT */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = EC_FLASH_PROTECT_RO_AT_BOOT;
	expected_flags |= EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Disable RO_AT_BOOT; should change nothing as GPIO WP_L is asserted */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = 0;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable ALL_NOW */
	params.mask = EC_FLASH_PROTECT_ALL_NOW;
	params.flags = EC_FLASH_PROTECT_ALL_NOW;
	expected_flags |= EC_FLASH_PROTECT_ALL_NOW;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Disable ALL_NOW; should change nothing as GPIO WP_L is asserted */
	params.mask = EC_FLASH_PROTECT_ALL_NOW;
	params.flags = 0;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Disable RO_AT_BOOT; should change nothing as GPIO WP_L is asserted */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = 0;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);
}

ZTEST_USER(flash, test_hostcmd_flash_protect_wp_deasserted)
{
	struct ec_response_flash_protect response;
	struct ec_params_flash_protect params = {
		.mask = 0,
		.flags = 0,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_FLASH_PROTECT, 0, response, params);
	/* The original flags 0 as GPIO WP_L deasserted */
	uint32_t expected_flags = 0;

	zassert_ok(gpio_wp_l_set(1), NULL);

	/* Get the flash protect */
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable RO_AT_BOOT */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = EC_FLASH_PROTECT_RO_AT_BOOT;
	expected_flags |= EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Disable RO_AT_BOOT */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = 0;
	expected_flags &=
		~(EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW);
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable RO_AT_BOOT */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = EC_FLASH_PROTECT_RO_AT_BOOT;
	expected_flags |= EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable ALL_NOW; should change nothing as GPIO WP_L is deasserted */
	params.mask = EC_FLASH_PROTECT_ALL_NOW;
	params.flags = EC_FLASH_PROTECT_ALL_NOW;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);
}

static void flash_reset(void)
{
	/* Set the GPIO WP_L to default */
	gpio_wp_l_set(0);

	/* Reset the protection flags */
	cros_flash_emul_protect_reset();
}

static void flash_before(void *data)
{
	ARG_UNUSED(data);
	flash_reset();
}

static void flash_after(void *data)
{
	ARG_UNUSED(data);
	flash_reset();
}

ZTEST_SUITE(flash, drivers_predicate_post_main, NULL, flash_before, flash_after,
	    NULL);
