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
#include "flash.h"
#include "host_command.h"
#include "system.h"
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

#define TEST_BUF_SIZE 0x100

ZTEST_USER(flash, test_hostcmd_flash_write_and_erase)
{
	uint8_t in_buf[TEST_BUF_SIZE];
	uint8_t out_buf[sizeof(struct ec_params_flash_write) + TEST_BUF_SIZE];

	struct ec_params_flash_read read_params = {
		.offset = 0x10000,
		.size = TEST_BUF_SIZE,
	};
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND(EC_CMD_FLASH_READ, 0, in_buf, read_params);

	struct ec_params_flash_erase erase_params = {
		.offset = 0x10000,
		.size = 0x10000,
	};
	struct host_cmd_handler_args erase_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_FLASH_ERASE, 0, erase_params);

	/* The write host command structs need to be filled run-time */
	struct ec_params_flash_write *write_params =
		(struct ec_params_flash_write *)out_buf;
	struct host_cmd_handler_args write_args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_FLASH_WRITE, 0);

	write_params->offset = 0x10000;
	write_params->size = TEST_BUF_SIZE;
	write_args.params = write_params;
	write_args.params_size = sizeof(*write_params) + TEST_BUF_SIZE;

	/* Flash write to all 0xec */
	memset(write_params + 1, 0xec, TEST_BUF_SIZE);
	zassert_ok(host_command_process(&write_args), NULL);

	/* Flash read and compare the readback data */
	zassert_ok(host_command_process(&read_args), NULL);
	zassert_equal(read_args.response_size, TEST_BUF_SIZE, NULL);
	zassert_equal(in_buf[0], 0xec, "readback data not expected: 0x%x",
		      in_buf[0]);
	zassert_equal(in_buf[TEST_BUF_SIZE - 1], 0xec,
		      "readback data not expected: 0x%x", in_buf[0]);

	/* Flash erase */
	zassert_ok(host_command_process(&erase_args), NULL);

	/* Flash read and compare the readback data */
	zassert_ok(host_command_process(&read_args), NULL);
	zassert_equal(in_buf[0], 0xff, "readback data not expected: 0x%x",
		      in_buf[0]);
	zassert_equal(in_buf[TEST_BUF_SIZE - 1], 0xff,
		      "readback data not expected: 0x%x", in_buf[0]);
}

#define EC_FLASH_REGION_START \
	MIN(CONFIG_EC_PROTECTED_STORAGE_OFF, CONFIG_EC_WRITABLE_STORAGE_OFF)

static void test_region_info(uint32_t region, uint32_t expected_offset,
			     uint32_t expected_size)
{
	struct ec_response_flash_region_info response;
	struct ec_params_flash_region_info params = {
		.region = region,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_FLASH_REGION_INFO, 1, response, params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.offset, expected_offset, NULL);
	zassert_equal(response.size, expected_size, NULL);
}

ZTEST_USER(flash, test_hostcmd_flash_region_info_ro)
{
	test_region_info(EC_FLASH_REGION_RO,
			 CONFIG_EC_PROTECTED_STORAGE_OFF +
				 CONFIG_RO_STORAGE_OFF - EC_FLASH_REGION_START,
			 EC_FLASH_REGION_RO_SIZE);
}

ZTEST_USER(flash, test_hostcmd_flash_region_info_active)
{
	test_region_info(EC_FLASH_REGION_ACTIVE,
			 flash_get_rw_offset(system_get_active_copy()) -
				 EC_FLASH_REGION_START,
			 CONFIG_EC_WRITABLE_STORAGE_SIZE);
}

ZTEST_USER(flash, test_hostcmd_flash_region_info_active_wp_ro)
{
	test_region_info(EC_FLASH_REGION_WP_RO,
			 CONFIG_WP_STORAGE_OFF - EC_FLASH_REGION_START,
			 CONFIG_WP_STORAGE_SIZE);
}

ZTEST_USER(flash, test_hostcmd_flash_region_info_active_update)
{
	test_region_info(EC_FLASH_REGION_UPDATE,
			 flash_get_rw_offset(system_get_update_copy()) -
				 EC_FLASH_REGION_START,
			 CONFIG_EC_WRITABLE_STORAGE_SIZE);
}

ZTEST_USER(flash, test_hostcmd_flash_region_info_active_invalid)
{
	struct ec_response_flash_region_info response;
	struct ec_params_flash_region_info params = {
		/* Get an invalid region */
		.region = 10,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_FLASH_REGION_INFO, 1, response, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM, NULL);
}

ZTEST_USER(flash, test_hostcmd_flash_info)
{
	struct ec_response_flash_info_1 response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_FLASH_INFO, 1, response);

	/* Get the flash info. */
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flash_size,
		      CONFIG_FLASH_SIZE_BYTES - EC_FLASH_REGION_START,
		      "response.flash_size = %d", response.flash_size);
	zassert_equal(response.flags, 0, "response.flags = %d", response.flags);
	zassert_equal(response.write_block_size, CONFIG_FLASH_WRITE_SIZE,
		      "response.write_block_size = %d",
		      response.write_block_size);
	zassert_equal(response.erase_block_size, CONFIG_FLASH_ERASE_SIZE,
		      "response.erase_block_size = %d",
		      response.erase_block_size);
	zassert_equal(response.protect_block_size, CONFIG_FLASH_BANK_SIZE,
		      "response.protect_block_size = %d",
		      response.protect_block_size);
	zassert_equal(
		response.write_ideal_size,
		(args.response_max - sizeof(struct ec_params_flash_write)) &
			~(CONFIG_FLASH_WRITE_SIZE - 1),
		"response.write_ideal_size = %d", response.write_ideal_size);
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

	/* The test modifies this bank. Erase it in case of failure. */
	crec_flash_erase(0x10000, 0x10000);
}

ZTEST_SUITE(flash, drivers_predicate_post_main, NULL, flash_before, flash_after,
	    NULL);
