/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "emul/emul_flash.h"
#include "flash.h"
#include "host_command.h"
#include "system.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define WP_L_GPIO_PATH NAMED_GPIOS_GPIO_NODE(wp_l)

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
	/* The original flags not 0 as GPIO WP_L asserted */
	uint32_t expected_flags = EC_FLASH_PROTECT_GPIO_ASSERTED;

	/* Get the flash protect */
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable RO_AT_BOOT */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = EC_FLASH_PROTECT_RO_AT_BOOT;
	expected_flags |= EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Disable RO_AT_BOOT; should change nothing as GPIO WP_L is asserted */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = 0;
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable ALL_NOW */
	params.mask = EC_FLASH_PROTECT_ALL_NOW;
	params.flags = EC_FLASH_PROTECT_ALL_NOW;
	expected_flags |= EC_FLASH_PROTECT_ALL_NOW;
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Disable ALL_NOW; should change nothing as GPIO WP_L is asserted */
	params.mask = EC_FLASH_PROTECT_ALL_NOW;
	params.flags = 0;
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Disable RO_AT_BOOT; should change nothing as GPIO WP_L is asserted */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = 0;
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
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
	/* The original flags 0 as GPIO WP_L deasserted */
	uint32_t expected_flags = 0;

	zassert_ok(gpio_wp_l_set(1), NULL);

	/* Get the flash protect */
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable RO_AT_BOOT */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = EC_FLASH_PROTECT_RO_AT_BOOT;
	expected_flags |= EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Disable RO_AT_BOOT */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = 0;
	expected_flags &=
		~(EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW);
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable RO_AT_BOOT */
	params.mask = EC_FLASH_PROTECT_RO_AT_BOOT;
	params.flags = EC_FLASH_PROTECT_RO_AT_BOOT;
	expected_flags |= EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);

	/* Enable ALL_NOW; should change nothing as GPIO WP_L is deasserted */
	params.mask = EC_FLASH_PROTECT_ALL_NOW;
	params.flags = EC_FLASH_PROTECT_ALL_NOW;
	zassert_ok(ec_cmd_flash_protect_v1(NULL, &params, &response), NULL);
	zassert_equal(response.flags, expected_flags, "response.flags = %d",
		      response.flags);
}

ZTEST_USER(flash, test_hostcmd_flash_read__overflow)
{
	struct ec_params_flash_read params = {
		.size = 32,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_FLASH_READ, 0, params);

	zassert_equal(EC_RES_OVERFLOW, host_command_process(&args));
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
	zassert_ok(ec_cmd_flash_erase(NULL, &erase_params), NULL);

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

	zassert_ok(ec_cmd_flash_region_info_v1(NULL, &params, &response), NULL);
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

	zassert_equal(ec_cmd_flash_region_info_v1(NULL, &params, &response),
		      EC_RES_INVALID_PARAM, NULL);
}

ZTEST_USER(flash, test_hostcmd_flash_info_1)
{
	struct ec_response_flash_info_1 response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_FLASH_INFO, 1, response);

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USE_ZEPHYR_FLASH_PAGE_LAYOUT)) {
		/* Get the flash info. */
		zassert_ok(host_command_process(&args), NULL);
		zassert_equal(args.response_size, sizeof(response));
		zassert_equal(response.flash_size,
			      CONFIG_FLASH_SIZE_BYTES - EC_FLASH_REGION_START,
			      "response.flash_size = %d", response.flash_size);
		zassert_equal(response.flags, 0, "response.flags = %d",
			      response.flags);
		zassert_equal(response.write_block_size,
			      CONFIG_FLASH_WRITE_SIZE,
			      "response.write_block_size = %d",
			      response.write_block_size);
		zassert_equal(response.erase_block_size,
			      CONFIG_FLASH_ERASE_SIZE,
			      "response.erase_block_size = %d",
			      response.erase_block_size);
		zassert_equal(response.protect_block_size,
			      CONFIG_FLASH_BANK_SIZE,
			      "response.protect_block_size = %d",
			      response.protect_block_size);
		zassert_equal(response.write_ideal_size,
			      (args.response_max -
			       sizeof(struct ec_params_flash_write)) &
				      ~(CONFIG_FLASH_WRITE_SIZE - 1),
			      "response.write_ideal_size = %d",
			      response.write_ideal_size);
	} else {
		/*
		 * Flash sector description not supported in FLASH_INFO
		 * version 1 command
		 */
		zassert_equal(ec_cmd_flash_info_v1(NULL, &response),
			      EC_RES_INVALID_VERSION, NULL);
	}
}

ZTEST_USER(flash, test_hostcmd_flash_info_2_zero_bank)
{
	struct ec_response_flash_info_2 response = {};
	struct ec_params_flash_info_2 params = {
		.num_banks_desc = 0,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_FLASH_INFO, 2, response, params);

	/* Get the flash info. */
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response.flash_size,
		      CONFIG_FLASH_SIZE_BYTES - EC_FLASH_REGION_START, "got %d",
		      response.flash_size);
	zassert_equal(response.flags, 0, "got %d", response.flags);
	zassert_equal(
		response.write_ideal_size,
		(args.response_max - sizeof(struct ec_params_flash_write)) &
			~(CONFIG_FLASH_WRITE_SIZE - 1),
		"got %d", response.write_ideal_size);
	zassert_equal(response.num_banks_total, 1, "got %d",
		      response.num_banks_total);
	zassert_equal(response.num_banks_desc, 0, "got %d",
		      response.num_banks_desc);
}

ZTEST_USER(flash, test_hostcmd_flash_info_2)
{
	uint8_t response_buffer[sizeof(struct ec_response_flash_info_2) +
				sizeof(struct ec_flash_bank)];
	struct ec_response_flash_info_2 *response =
		(struct ec_response_flash_info_2 *)response_buffer;
	struct ec_params_flash_info_2 params = {
		.num_banks_desc = 1,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_FLASH_INFO, 2, response_buffer, params);

	/* Get the flash info. */
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(response->flash_size,
		      CONFIG_FLASH_SIZE_BYTES - EC_FLASH_REGION_START, "got %d",
		      response->flash_size);
	zassert_equal(response->flags, 0, "got %d", response->flags);
	zassert_equal(
		response->write_ideal_size,
		(args.response_max - sizeof(struct ec_params_flash_write)) &
			~(CONFIG_FLASH_WRITE_SIZE - 1),
		"got %d", response->write_ideal_size);
	zassert_equal(response->num_banks_total, 1, "got %d",
		      response->num_banks_total);
	zassert_equal(response->num_banks_desc, 1, "got %d",
		      response->num_banks_desc);
	zassert_equal(response->banks[0].count,
		      CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE,
		      "got %d", response->banks[0].count);
	zassert_equal(response->banks[0].size_exp,
		      __fls(CONFIG_FLASH_BANK_SIZE), "got %d",
		      response->banks[0].size_exp);
	zassert_equal(response->banks[0].write_size_exp,
		      __fls(CONFIG_FLASH_WRITE_SIZE), "got %d",
		      response->banks[0].write_size_exp);
	zassert_equal(response->banks[0].erase_size_exp,
		      __fls(CONFIG_FLASH_ERASE_SIZE), "got %d",
		      response->banks[0].erase_size_exp);
	zassert_equal(response->banks[0].protect_size_exp,
		      __fls(CONFIG_FLASH_BANK_SIZE), "got %d",
		      response->banks[0].protect_size_exp);
}

ZTEST_USER(flash, test_console_cmd_flash_info)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;
	/* Arbitrary array size for sprintf should not need this amount */
	char format_buffer[100];

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "flashinfo"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0, NULL);

	sprintf(format_buffer, "Usable:  %4d KB",
		CONFIG_FLASH_SIZE_BYTES / 1024);
	zassert_not_null(strstr(outbuffer, format_buffer));

	sprintf(format_buffer, "Write:   %4d B (ideal %d B)",
		CONFIG_FLASH_WRITE_SIZE, CONFIG_FLASH_WRITE_IDEAL_SIZE);
	zassert_not_null(strstr(outbuffer, format_buffer));

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USE_ZEPHYR_FLASH_PAGE_LAYOUT)) {
		sprintf(format_buffer, "%d regions", crec_flash_total_banks());
		zassert_not_null(strstr(outbuffer, format_buffer));
	}

	sprintf(format_buffer, "Erase:   %4d B", CONFIG_FLASH_ERASE_SIZE);
	zassert_not_null(strstr(outbuffer, format_buffer));

	sprintf(format_buffer, "Protect: %4d B", CONFIG_FLASH_BANK_SIZE);
	zassert_not_null(strstr(outbuffer, format_buffer));

	zassert_not_null(strstr(outbuffer, "wp_gpio_asserted: ON"));
	zassert_not_null(strstr(outbuffer, "ro_at_boot: OFF"));
	zassert_not_null(strstr(outbuffer, "all_at_boot: OFF"));
	zassert_not_null(strstr(outbuffer, "ro_now: OFF"));
	zassert_not_null(strstr(outbuffer, "all_now: OFF"));
	zassert_not_null(strstr(outbuffer, "STUCK: OFF"));
	zassert_not_null(strstr(outbuffer, "INCONSISTENT: OFF"));
	zassert_not_null(strstr(outbuffer, "UNKNOWN_ERROR: OFF"));
	zassert_not_null(strstr(outbuffer, "Protected now"));
}

ZTEST_USER(flash, test_console_cmd_flashwp__invalid)
{
	/* Command requires a 2nd CLI arg */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "flashwp"), NULL);
}

ZTEST_USER(flash, test_console_cmd_flashwp__now)
{
	uint32_t current;

	zassert_ok(shell_execute_cmd(get_ec_shell(), "flashwp true"), NULL);

	current = crec_flash_get_protect();
	zassert_true(EC_FLASH_PROTECT_GPIO_ASSERTED & current, "current = %08x",
		     current);
	zassert_true(EC_FLASH_PROTECT_RO_AT_BOOT & current, "current = %08x",
		     current);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "flashwp now"), NULL);

	current = crec_flash_get_protect();
	zassert_true(current & EC_FLASH_PROTECT_ALL_NOW, "current = %08x",
		     current);
}

ZTEST_USER(flash, test_console_cmd_flashwp__all)
{
	uint32_t current;

	zassert_ok(shell_execute_cmd(get_ec_shell(), "flashwp true"), NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "flashwp all"), NULL);

	current = crec_flash_get_protect();
	zassert_true(EC_FLASH_PROTECT_ALL_NOW & current, "current = %08x",
		     current);
}

ZTEST_USER(flash, test_console_cmd_flashwp__bool_false)
{
	uint32_t current;

	/* Set RO_AT_BOOT and verify */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "flashwp true"), NULL);

	current = crec_flash_get_protect();
	zassert_true(current & EC_FLASH_PROTECT_RO_AT_BOOT, "current = %08x",
		     current);

	gpio_wp_l_set(1);

	/* Now clear it */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "flashwp false"), NULL);

	current = crec_flash_get_protect();
	zassert_false(current & EC_FLASH_PROTECT_RO_AT_BOOT, "current = %08x",
		      current);
}

ZTEST_USER(flash, test_console_cmd_flashwp__bool_true)
{
	uint32_t current;

	gpio_wp_l_set(1);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "flashwp true"), NULL);

	current = crec_flash_get_protect();
	zassert_equal(EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW,
		      current, "current = %08x", current);
}

ZTEST_USER(flash, test_console_cmd_flashwp__bad_param)
{
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "flashwp xyz"), NULL);
}

ZTEST_USER(flash, test_console_cmd_flash_erase__flash_locked)
{
	/* Force write protection on */
	zassert_ok(crec_flash_physical_protect_now(1));

	CHECK_CONSOLE_CMD("flasherase 0x1000 0x1000", NULL,
			  EC_ERROR_ACCESS_DENIED);
}

ZTEST_USER(flash, test_console_cmd_flash_erase__bad_args)
{
	/* No args*/
	CHECK_CONSOLE_CMD("flasherase", NULL, EC_ERROR_PARAM_COUNT);

	/* Check for 1 of 2 required args */
	CHECK_CONSOLE_CMD("flasherase 0x1000", NULL, EC_ERROR_PARAM_COUNT);

	/* Check for alpha arg instead of number*/
	CHECK_CONSOLE_CMD("flasherase xyz 100", NULL, EC_ERROR_PARAM1);
	CHECK_CONSOLE_CMD("flasherase 100 xyz", NULL, EC_ERROR_PARAM2);
}

/**
 * @brief Writes a 32-bit word at a specific location in flash memory. Uses Host
 *        Command interface to communicate with flash driver.
 *
 * @param offset Address to begin writing at.
 * @param data A 32-bit word to write.
 * @return uint16_t Host command return status.
 */
static uint16_t write_flash_helper32(uint32_t offset, uint32_t data)
{
	uint8_t out_buf[sizeof(struct ec_params_flash_write) + sizeof(data)];

	/* The write host command structs need to be filled run-time */
	struct ec_params_flash_write *write_params =
		(struct ec_params_flash_write *)out_buf;
	struct host_cmd_handler_args write_args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_FLASH_WRITE, 0);

	write_params->offset = offset;
	write_params->size = sizeof(data);
	write_args.params = write_params;
	write_args.params_size = sizeof(*write_params) + sizeof(data);

	/* Flash write `data` */
	memcpy(write_params + 1, &data, sizeof(data));
	return host_command_process(&write_args);
}

/**
 * @brief Reads a 32-bit word at a specific location in flash memory. Uses Host
 *        Command interface to communicate with flash driver.
 *
 * @param offset Address to begin reading from.
 * @param data Output param for 32-bit read data.
 * @return uint16_t Host command return status.
 */
static uint16_t read_flash_helper32(uint32_t offset, uint32_t *output)
{
	struct ec_params_flash_read read_params = {
		.offset = offset,
		.size = sizeof(*output),
	};
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND(EC_CMD_FLASH_READ, 0, *output, read_params);
	/* Flash read and compare the readback data */
	int ret = host_command_process(&read_args);

	if (ret == EC_RES_SUCCESS) {
		zassert_equal(read_args.response_size, sizeof(*output));
	}
	return ret;
}

ZTEST_USER(flash, test_console_cmd_flash_erase__happy)
{
	/* Immediately before the region to erase */
	zassert_ok(write_flash_helper32(0x10000 - 4, 0x5A5A5A5A));

	/* Start and end of the region we will erase */
	zassert_ok(write_flash_helper32(0x10000, 0xA1B2C3D4));
	zassert_ok(write_flash_helper32(0x20000 - 4, 0x1A2B3C4D));

	/* Immediately after the region to erase */
	zassert_ok(write_flash_helper32(0x20000, 0xA5A5A5A5));

	CHECK_CONSOLE_CMD("flasherase 0x10000 0x10000", NULL, EC_SUCCESS);

	uint32_t output;

	/* These should remain untouched */
	zassert_ok(read_flash_helper32(0x10000 - 4, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x20000, &output));
	zassert_equal(output, 0xA5A5A5A5, "Got %08x", output);

	/* These are within the erase region and should be reset to all FF */
	zassert_ok(read_flash_helper32(0x10000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x20000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

#ifdef CONFIG_PLATFORM_EC_CBI_FLASH
ZTEST_USER(flash, test_console_cmd_flash_erase__cbi_overlap_0)
{
	uint32_t output;

	/* Immediately before the region to erase */
	zassert_ok(write_flash_helper32(0x3F000 - 4, 0x5A5A5A5A));
	/* Immediately after the region to erase */
	zassert_ok(write_flash_helper32(0x40000, 0xA5A5A5A5));

	/* The CBI region */
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);

	CHECK_CONSOLE_CMD("flasherase 0x3F000 0x1000", NULL, EC_SUCCESS);

	/* These should remain untouched */
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xA5A5A5A5, "Got %08x", output);

	/* These are within the erase region and should be reset to all FF */
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

ZTEST_USER(flash, test_console_cmd_flash_erase__cbi_overlap_left)
{
	uint32_t output;

	/* Immediately before the region to erase */
	zassert_ok(write_flash_helper32(0x3F000 - 4, 0x5A5A5A5A));
	/* Immediately after the region to erase */
	zassert_ok(write_flash_helper32(0x50000, 0x5A5A5A5A));
	/* Immediately after the CBI region */
	zassert_ok(write_flash_helper32(0x40000, 0xA5A5A5A5));
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xA5A5A5A5, "Got %08x", output);

	/* The CBI region */
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);

	CHECK_CONSOLE_CMD("flasherase 0x3F000 0x11000", NULL, EC_SUCCESS);

	/* These should remain untouched */
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x50000, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);

	/* These are within the erase region and should be reset to all FF */
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x50000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

ZTEST_USER(flash, test_console_cmd_flash_erase__cbi_overlap_right)
{
	uint32_t output;

	/* Immediately before the region to erase */
	zassert_ok(write_flash_helper32(0x2F000 - 4, 0x5A5A5A5A));
	/* Immediately after the region to erase */
	zassert_ok(write_flash_helper32(0x40000, 0xA5A5A5A5));
	/* Immediately before the CBI region */
	zassert_ok(write_flash_helper32(0x3F000 - 4, 0xA5A5A5AA));
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0xA5A5A5AA, "Got %08x", output);

	/* The CBI region */
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);

	CHECK_CONSOLE_CMD("flasherase 0x2F000 0x11000", NULL, EC_SUCCESS);

	/* These should remain untouched */
	zassert_ok(read_flash_helper32(0x2F000 - 4, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xA5A5A5A5, "Got %08x", output);

	/* These are within the erase region and should be reset to all FF */
	zassert_ok(read_flash_helper32(0x2F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

ZTEST_USER(flash, test_console_cmd_flash_erase__left)
{
	uint32_t output;

	/* Immediately before the region to erase */
	zassert_ok(write_flash_helper32(0x3E000 - 4, 0x5A5A5A5A));
	zassert_ok(write_flash_helper32(0x3E000, 0x5A5AAA5A));
	/* Immediately before the CBI region */
	zassert_ok(write_flash_helper32(0x3F000 - 4, 0x5A5A5AAA));
	/* Immediately after the CBI region */
	zassert_ok(write_flash_helper32(0x40000, 0xA5A5A5A5));

	/* The CBI region */
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);

	CHECK_CONSOLE_CMD("flasherase 0x3E000 0x1000", NULL, EC_SUCCESS);

	/* These should remain untouched */
	zassert_ok(read_flash_helper32(0x3E000 - 4, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xA5A5A5A5, "Got %08x", output);

	/* These are within the erase region and should be reset to all FF */
	zassert_ok(read_flash_helper32(0x3E000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

ZTEST_USER(flash, test_console_cmd_flash_erase__right)
{
	uint32_t output;

	/* Immediately before the CBI region */
	zassert_ok(write_flash_helper32(0x3F000 - 4, 0x5A5A5AAA));
	/* Immediately after the CBI region */
	zassert_ok(write_flash_helper32(0x40000, 0xA5A5A5A5));
	zassert_ok(write_flash_helper32(0x41000 - 4, 0x555A5A5A));
	/* Immediately after the region to erase */
	zassert_ok(write_flash_helper32(0x41000, 0x5A5A5A5A));

	/* The CBI region */
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);

	CHECK_CONSOLE_CMD("flasherase 0x40000 0x1000", NULL, EC_SUCCESS);

	/* These should remain untouched */
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0x5A5A5AAA, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x41000, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);

	/* These are within the erase region and should be reset to all FF */
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x41000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

ZTEST_USER(flash, test_console_cmd_flash_erase__cbi_overlap_1)
{
	uint32_t output;

	/* Immediately before the region to erase */
	zassert_ok(write_flash_helper32(0x20000 - 4, 0x5A5A5A5A));
	/* Immediately after the region to erase */
	zassert_ok(write_flash_helper32(0x50000, 0xA5A5A5A5));
	/* Immediately before the CBI region */
	zassert_ok(write_flash_helper32(0x3F000 - 4, 0x555A5A5A));
	/* Immediately after the CBI region */
	zassert_ok(write_flash_helper32(0x40000, 0xA5A555A5));

	/* The CBI region */
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0x555A5A5A, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xA5A555A5, "Got %08x", output);

	CHECK_CONSOLE_CMD("flasherase 0x20000 0x30000", NULL, EC_SUCCESS);

	/* These should remain untouched */
	zassert_ok(read_flash_helper32(0x20000 - 4, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x50000, &output));
	zassert_equal(output, 0xA5A5A5A5, "Got %08x", output);

	/* These are within the erase region and should be reset to all FF */
	zassert_ok(read_flash_helper32(0x20000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x50000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

ZTEST_USER(flash, test_console_cmd_flash_write__cbi_0)
{
	uint32_t output;

	/* Immediately before the CBI region */
	zassert_ok(write_flash_helper32(0x3F000 - 4, 0x555A5A5A));
	zassert_ok(read_flash_helper32(0x3F000 - 4, &output));
	zassert_equal(output, 0x555A5A5A, "Got %08x", output);
	/* Immediately after the CBI region */
	zassert_ok(write_flash_helper32(0x40000, 0xA5A555A5));
	zassert_ok(read_flash_helper32(0x40000, &output));
	zassert_equal(output, 0xA5A555A5, "Got %08x", output);

	/* The CBI region */
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);

	zassert_ok(write_flash_helper32(0x10000, 0x5A5A5A5A));
	zassert_ok(read_flash_helper32(0x10000, &output));
	zassert_equal(output, 0x5A5A5A5A, "Got %08x", output);

	zassert_ok(write_flash_helper32(0x50000, 0x5AAA5A5A));
	zassert_ok(read_flash_helper32(0x50000, &output));
	zassert_equal(output, 0x5AAA5A5A, "Got %08x", output);

	zassert_ok(write_flash_helper32(0x3F000, 0x5A5A5A5A));
	zassert_ok(read_flash_helper32(0x3F000, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);

	zassert_ok(write_flash_helper32(0x40000 - 4, 0x5AAA5A5A));
	zassert_ok(read_flash_helper32(0x40000 - 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

ZTEST_USER(flash, test_console_cmd_flash_write__cbi_1)
{
	uint32_t output;

	/* At the start of CBI region */
	zassert_ok(write_flash_helper32(0x3F000 - 2, 0x555A5A5A));
	zassert_ok(read_flash_helper32(0x3F000 - 2, &output));
	zassert_equal(output, 0xFFFF5A5A, "Got %08x", output);

	/* At the end of CBI region */
	zassert_ok(write_flash_helper32(0x40000 - 2, 0xA5A555A5));
	zassert_ok(read_flash_helper32(0x40000 - 2, &output));
	zassert_equal(output, 0xA5A5FFFF, "Got %08x", output);
}
#endif

ZTEST_USER(flash, test_console_cmd_flash_write__flash_locked)
{
	/* Force write protection on */
	zassert_ok(crec_flash_physical_protect_now(1));

	CHECK_CONSOLE_CMD("flashwrite 0x1000 0x1000", NULL,
			  EC_ERROR_ACCESS_DENIED);
}

ZTEST_USER(flash, test_console_cmd_flash_write__bad_args)
{
	/* No args*/
	CHECK_CONSOLE_CMD("flashwrite", NULL, EC_ERROR_PARAM_COUNT);

	/* Check for 1 of 2 required args */
	CHECK_CONSOLE_CMD("flashwrite 0x1000", NULL, EC_ERROR_PARAM_COUNT);

	/* Check for alpha arg instead of number*/
	CHECK_CONSOLE_CMD("flashwrite xyz 100", NULL, EC_ERROR_PARAM1);
	CHECK_CONSOLE_CMD("flashwrite 100 xyz", NULL, EC_ERROR_PARAM2);
}

ZTEST_USER(flash, test_console_cmd_flash_write__too_big)
{
	CHECK_CONSOLE_CMD("flashwrite 0x10000 " STRINGIFY(INT_MAX), NULL,
			  EC_ERROR_INVAL);
}

ZTEST_USER(flash, test_console_cmd_flash_write__happy)
{
	/* Write 4 bytes. The bytes written are autogenerated and just the
	 * pattern 00 01 02 03.
	 */
	CHECK_CONSOLE_CMD("flashwrite 0x10000 4", NULL, EC_SUCCESS);

	uint32_t output;
	static const uint8_t expected[] = { 0x00, 0x01, 0x02, 0x03 };

	/* Check for pattern */
	zassert_ok(read_flash_helper32(0x10000, &output));
	zassert_mem_equal(&output, &expected, sizeof(expected));

	/* Check the space after to ensure it is still erased. */
	zassert_ok(read_flash_helper32(0x10000 + 4, &output));
	zassert_equal(output, 0xFFFFFFFF, "Got %08x", output);
}

ZTEST_USER(flash, test_console_cmd_flash_read__bad_args)
{
	/* No args*/
	CHECK_CONSOLE_CMD("flashread", NULL, EC_ERROR_PARAM_COUNT);

	/* Check for alpha arg instead of number*/
	CHECK_CONSOLE_CMD("flashread xyz 100", NULL, EC_ERROR_PARAM1);
	CHECK_CONSOLE_CMD("flashread 100 xyz", NULL, EC_ERROR_PARAM2);
}

ZTEST_USER(flash, test_console_cmd_flash_read__too_big)
{
	CHECK_CONSOLE_CMD("flashread 0x10000 " STRINGIFY(INT_MAX), NULL,
			  EC_ERROR_INVAL);
}

ZTEST_USER(flash, test_console_cmd_flash_read__happy_4_bytes)
{
	/* Write some bytes to read */
	zassert_ok(write_flash_helper32(0x10000, sys_cpu_to_be32(0xA1B2C3D4)));

	static const char *expected = "\r\n\r\n"
				      "00010000: a1 b2 c3 d4\r\n";
	CHECK_CONSOLE_CMD("flashread 0x10000 4", expected, EC_SUCCESS);
}

ZTEST_USER(flash, test_console_cmd_flash_read__happy_17_bytes)
{
	/* Test 16-byte column wrapping behavior */

	/* Write some bytes to read */
	zassert_ok(write_flash_helper32(0x10000, sys_cpu_to_be32(0xA1B2C3D4)));

	static const char *expected =
		"\r\n\r\n"
		"00010000: a1 b2 c3 d4 ff ff ff ff ff ff ff ff ff ff ff ff\r\n"
		"00010010: ff\r\n";

	CHECK_CONSOLE_CMD("flashread 0x10000 17", expected, EC_SUCCESS);
}

/**
 * @brief Prepare a region of flash for the test_crec_flash_is_erased* tests
 *
 * @param offset Offset to write bytes at.
 * @param size Number of bytes to erase.
 * @param make_write If true, write an arbitrary byte after erase so the region
 *                   is no longer fully erased.
 */
static void setup_flash_region_helper(uint32_t offset, uint32_t size,
				      bool make_write)
{
	struct ec_params_flash_erase erase_params = {
		.offset = offset,
		.size = size,
	};
	int rv;

	rv = ec_cmd_flash_erase(NULL, &erase_params);
	zassert_ok(rv, "Got %d", rv);

	if (make_write) {
		/* Sized for flash_write header plus one byte of data */
		uint8_t out_buf[sizeof(struct ec_params_flash_write) +
				sizeof(uint8_t)];

		struct ec_params_flash_write *write_params =
			(struct ec_params_flash_write *)out_buf;
		struct host_cmd_handler_args write_args =
			BUILD_HOST_COMMAND_SIMPLE(EC_CMD_FLASH_WRITE, 0);

		write_params->offset = offset;
		write_params->size = 1;
		write_args.params = write_params;
		write_args.params_size = sizeof(out_buf);

		/* Write one byte at start of region */
		out_buf[sizeof(*write_params)] = 0xec;

		zassert_ok(host_command_process(&write_args), NULL);
	}
}

ZTEST_USER(flash, test_crec_flash_is_erased__happy)
{
	uint32_t offset = 0x10000;

	setup_flash_region_helper(offset, CONFIG_FLASH_ERASE_SIZE, false);

	zassert_true(crec_flash_is_erased(offset, CONFIG_FLASH_ERASE_SIZE),
		     NULL);
}

ZTEST_USER(flash, test_crec_flash_is_erased__not_erased)
{
	uint32_t offset = 0x10000;

	setup_flash_region_helper(offset, CONFIG_FLASH_ERASE_SIZE, true);

	zassert_true(!crec_flash_is_erased(offset, CONFIG_FLASH_ERASE_SIZE),
		     NULL);
}

static void flash_reset(void *data)
{
	ARG_UNUSED(data);

	/* Set the GPIO WP_L to default */
	zassert_ok(gpio_wp_l_set(0));

	/* Reset the protection flags */
	cros_flash_emul_protect_reset();
	zassert_ok(crec_flash_physical_protect_now(0));

	/* Tests modify these banks. Erase them. */
	zassert_ok(crec_flash_erase(0x10000, 0x10000));
	zassert_ok(crec_flash_erase(0x30000, 0x10000));
	zassert_ok(crec_flash_erase(0x40000, 0x10000));
	zassert_ok(crec_flash_erase(0x50000, 0x10000));
}

ZTEST_SUITE(flash, drivers_predicate_post_main, NULL, flash_reset, flash_reset,
	    NULL);
