/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "system.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#define PORT 0

#define AC_OK_OD_GPIO_NAME "acok_od"

FAKE_VALUE_FUNC(int, system_is_locked);

static void espi_before(void *state)
{
	ARG_UNUSED(state);
	RESET_FAKE(system_is_locked);
}

static void espi_after(void *state)
{
	ARG_UNUSED(state);
	RESET_FAKE(system_is_locked);
}

ZTEST_USER(espi, test_host_command_get_protocol_info)
{
	struct ec_response_get_protocol_info response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PROTOCOL_INFO, 0, response);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.protocol_versions, BIT(3), NULL);
	zassert_equal(response.max_request_packet_size, EC_LPC_HOST_PACKET_SIZE,
		      NULL);
	zassert_equal(response.max_response_packet_size,
		      EC_LPC_HOST_PACKET_SIZE, NULL);
	zassert_equal(response.flags, 0, NULL);
}

ZTEST_USER(espi, test_host_command_usb_pd_power_info)
{
	/* Only test we've enabled the command */
	struct ec_response_usb_pd_power_info response;
	struct ec_params_usb_pd_power_info params = { .port = PORT };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_POWER_INFO, 0, response, params);

	args.params = &params;
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
}

ZTEST_USER(espi, test_host_command_typec_status)
{
	/* Only test we've enabled the command */
	struct ec_params_typec_status params = { .port = PORT };
	struct ec_response_typec_status response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_TYPEC_STATUS, 0, response, params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
}

ZTEST_USER(espi, test_host_command_usb_pd_get_amode)
{
	/* Only test we've enabled the command */
	struct ec_params_usb_pd_get_mode_request params = {
		.port = PORT,
		.svid_idx = 0,
	};
	struct ec_params_usb_pd_get_mode_response response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_GET_AMODE, 0, response, params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	/* Note: with no SVIDs the response size is the size of the svid field.
	 * See the usb alt mode test for verifying larger struct sizes
	 */
	zassert_equal(args.response_size, sizeof(response.svid), NULL);
}

ZTEST_USER(espi, test_host_command_gpio_get_v0)
{
	struct ec_params_gpio_get p = {
		/* Checking for AC enabled */
		.name = AC_OK_OD_GPIO_NAME,
	};
	struct ec_response_gpio_get response;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GPIO_GET, 0, response, p);

	set_ac_enabled(true);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_true(response.val, NULL);

	set_ac_enabled(false);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_false(response.val, NULL);
}

ZTEST_USER(espi, test_host_command_gpio_get_v1_get_by_name)
{
	struct ec_params_gpio_get_v1 p = {
		.subcmd = EC_GPIO_GET_BY_NAME,
		/* Checking for AC enabled */
		.get_value_by_name = {
			   AC_OK_OD_GPIO_NAME,
		},
	};
	struct ec_response_gpio_get_v1 response;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GPIO_GET, 1, response, p);

	set_ac_enabled(true);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response.get_value_by_name),
		      NULL);
	zassert_true(response.get_info.val, NULL);

	set_ac_enabled(false);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response.get_value_by_name),
		      NULL);
	zassert_false(response.get_info.val, NULL);
}

ZTEST_USER(espi, test_host_command_gpio_get_v1_get_count)
{
	struct ec_params_gpio_get_v1 p = {
		.subcmd = EC_GPIO_GET_COUNT,
	};
	struct ec_response_gpio_get_v1 response;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GPIO_GET, 1, response, p);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response.get_count), NULL);
	zassert_equal(response.get_count.val, GPIO_COUNT, NULL);
}

ZTEST_USER(espi, test_host_command_gpio_get_v1_get_info)
{
	const enum gpio_signal signal = GPIO_SIGNAL(DT_NODELABEL(gpio_acok_od));
	struct ec_params_gpio_get_v1 p = {
		.subcmd = EC_GPIO_GET_INFO,
		.get_info = {
			.index = signal,
		},
	};
	struct ec_response_gpio_get_v1 response;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GPIO_GET, 1, response, p);

	set_ac_enabled(true);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_ok(strcmp(response.get_info.name, AC_OK_OD_GPIO_NAME), NULL);
	zassert_true(response.get_info.val, NULL);

	set_ac_enabled(false);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_ok(strcmp(response.get_info.name, AC_OK_OD_GPIO_NAME), NULL);
	zassert_false(response.get_info.val, NULL);
}

ZTEST_USER(espi, test_host_command_gpio_set)
{
	struct nothing {
		int place_holder;
	};
	const struct gpio_dt_spec *gp = GPIO_DT_FROM_NODELABEL(gpio_test);
	struct ec_params_gpio_set p = {
		.name = "test",
		.val = 0,
	};

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_GPIO_SET, 0, p);

	/* Force value to 1 to see change */
	zassume_ok(gpio_pin_set_dt(gp, 1), NULL);

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(gpio_pin_get_dt(gp), p.val, NULL);

	p.val = 1;

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(gpio_pin_get_dt(gp), p.val, NULL);
}

ZTEST(espi, test_hc_gpio_get_v0_invalid_name)
{
	struct ec_response_gpio_get response;
	struct ec_params_gpio_get params = { .name = "INVALID_GPIO_NAME" };
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GPIO_GET, 0, response, params);

	zassert_equal(EC_RES_ERROR, host_command_process(&args), NULL);
}

ZTEST(espi, test_hc_gpio_get_v1_get_by_name_invalid_name)
{
	struct ec_response_gpio_get_v1 response;
	struct ec_params_gpio_get_v1 params = {
		.subcmd = EC_GPIO_GET_BY_NAME,
		.get_value_by_name.name = "INVALID_GPIO_NAME",
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GPIO_GET, 1, response, params);

	zassert_equal(EC_RES_ERROR, host_command_process(&args), NULL);
}

ZTEST(espi, test_hc_gpio_get_v1_get_info_invalid_index)
{
	struct ec_response_gpio_get_v1 response;
	struct ec_params_gpio_get_v1 params = {
		.subcmd = EC_GPIO_GET_INFO,
		.get_info.index = GPIO_COUNT,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GPIO_GET, 1, response, params);

	zassert_equal(EC_RES_ERROR, host_command_process(&args), NULL);
}

ZTEST(espi, test_hc_gpio_get_v1_invalid_subcmd)
{
	struct ec_response_gpio_get_v1 response;
	struct ec_params_gpio_get_v1 params = {
		.subcmd = EC_CMD_GPIO_GET,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GPIO_GET, 1, response, params);

	zassert_equal(EC_RES_INVALID_PARAM, host_command_process(&args), NULL);
}

/* EC_CMD_GET_FEATURES */
ZTEST_USER(espi, test_host_command_ec_cmd_get_features)
{
	struct ec_response_get_features response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_GET_FEATURES, 0, response);

	int rv = host_command_process(&args);

	zassert_equal(rv, EC_RES_SUCCESS, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);

	/* Check features returned */
	uint32_t feature_mask;

	feature_mask = EC_FEATURE_MASK_0(EC_FEATURE_FLASH);
	feature_mask |= EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE);
	feature_mask |= EC_FEATURE_MASK_0(EC_FEATURE_KEYB);
	zassert_true((response.flags[0] & feature_mask),
		     "Known features were not returned.");
	feature_mask = EC_FEATURE_MASK_1(EC_FEATURE_UNIFIED_WAKE_MASKS);
	feature_mask |= EC_FEATURE_MASK_1(EC_FEATURE_HOST_EVENT64);
	feature_mask |= EC_FEATURE_MASK_1(EC_FEATURE_EXEC_IN_RAM);
	zassert_true((response.flags[1] & feature_mask),
		     "Known features were not returned.");
}

ZTEST(espi, test_hc_gpio_set_system_is_locked)
{
	struct ec_params_gpio_set params;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_GPIO_SET, 0, params);

	system_is_locked_fake.return_val = 1;
	zassert_equal(EC_RES_ACCESS_DENIED, host_command_process(&args), NULL);
}

ZTEST(espi, test_hc_gpio_set_invalid_gpio_name)
{
	struct ec_params_gpio_set params = {
		.name = "",
		.val = 0,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_GPIO_SET, 0, params);

	zassert_equal(EC_RES_ERROR, host_command_process(&args), NULL);
}

ZTEST_SUITE(espi, drivers_predicate_post_main, NULL, espi_before, espi_after,
	    NULL);
