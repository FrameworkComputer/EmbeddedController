/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "system.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define PORT 0

#define AC_OK_OD_GPIO_NAME "acok_od"

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
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_get_protocol_info(&args, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.protocol_versions, BIT(3));
	/* Don't check max sizes for upstream, because it is adjusted
	 * to buffer sizes set by a backend.
	 */
#ifndef CONFIG_EC_HOST_CMD
	zassert_equal(response.max_request_packet_size, EC_LPC_HOST_PACKET_SIZE,
		      NULL);
	zassert_equal(response.max_response_packet_size,
		      EC_LPC_HOST_PACKET_SIZE, NULL);
#endif
	zassert_equal(response.flags, 0);
}

ZTEST_USER(espi, test_host_command_usb_pd_power_info)
{
	/* Only test we've enabled the command */
	struct ec_response_usb_pd_power_info response;
	struct ec_params_usb_pd_power_info params = { .port = PORT };
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_usb_pd_power_info(&args, &params, &response));
	zassert_equal(args.response_size, sizeof(response));
}

ZTEST_USER(espi, test_host_command_typec_status)
{
	/* Only test we've enabled the command */
	struct ec_params_typec_status params = { .port = PORT };
	struct ec_response_typec_status response;
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_typec_status(&args, &params, &response));
	zassert_equal(args.response_size, sizeof(response));
}

ZTEST_USER(espi, test_host_command_gpio_get_v0)
{
	struct ec_params_gpio_get p = {
		/* Checking for AC enabled */
		.name = AC_OK_OD_GPIO_NAME,
	};
	struct ec_response_gpio_get response;

	struct host_cmd_handler_args args;

	set_ac_enabled(true);

	zassert_ok(ec_cmd_gpio_get(&args, &p, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_true(response.val);

	set_ac_enabled(false);

	zassert_ok(ec_cmd_gpio_get(&args, &p, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_false(response.val);
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

	struct host_cmd_handler_args args;

	set_ac_enabled(true);

	zassert_ok(ec_cmd_gpio_get_v1(&args, &p, &response));
	zassert_equal(args.response_size, sizeof(response.get_value_by_name),
		      NULL);
	zassert_true(response.get_info.val);

	set_ac_enabled(false);

	zassert_ok(ec_cmd_gpio_get_v1(&args, &p, &response));
	zassert_equal(args.response_size, sizeof(response.get_value_by_name),
		      NULL);
	zassert_false(response.get_info.val);
}

ZTEST_USER(espi, test_host_command_gpio_get_v1_get_count)
{
	struct ec_params_gpio_get_v1 p = {
		.subcmd = EC_GPIO_GET_COUNT,
	};
	struct ec_response_gpio_get_v1 response;

	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_gpio_get_v1(&args, &p, &response), NULL);
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

	struct host_cmd_handler_args args;

	set_ac_enabled(true);

	zassert_ok(ec_cmd_gpio_get_v1(&args, &p, &response), NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_ok(strcmp(response.get_info.name, AC_OK_OD_GPIO_NAME), NULL);
	zassert_true(response.get_info.val, NULL);

	set_ac_enabled(false);

	zassert_ok(ec_cmd_gpio_get_v1(&args, &p, &response), NULL);
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

	/* Force value to 1 to see change */
	zassert_ok(gpio_pin_set_dt(gp, 1), NULL);

	zassert_ok(ec_cmd_gpio_set(NULL, &p), NULL);
	zassert_equal(gpio_pin_get_dt(gp), p.val, NULL);

	p.val = 1;

	zassert_ok(ec_cmd_gpio_set(NULL, &p), NULL);
	zassert_equal(gpio_pin_get_dt(gp), p.val, NULL);
}

ZTEST(espi, test_hc_gpio_get_v0_invalid_name)
{
	struct ec_response_gpio_get response;
	struct ec_params_gpio_get params = { .name = "INVALID_GPIO_NAME" };

	zassert_equal(EC_RES_ERROR, ec_cmd_gpio_get(NULL, &params, &response),
		      NULL);
}

ZTEST(espi, test_hc_gpio_get_v1_get_by_name_invalid_name)
{
	struct ec_response_gpio_get_v1 response;
	struct ec_params_gpio_get_v1 params = {
		.subcmd = EC_GPIO_GET_BY_NAME,
		.get_value_by_name.name = "INVALID_GPIO_NAME",
	};

	zassert_equal(EC_RES_ERROR,
		      ec_cmd_gpio_get_v1(NULL, &params, &response), NULL);
}

ZTEST(espi, test_hc_gpio_get_v1_get_info_invalid_index)
{
	struct ec_response_gpio_get_v1 response;
	struct ec_params_gpio_get_v1 params = {
		.subcmd = EC_GPIO_GET_INFO,
		.get_info.index = GPIO_COUNT,
	};

	zassert_equal(EC_RES_ERROR,
		      ec_cmd_gpio_get_v1(NULL, &params, &response), NULL);
}

ZTEST(espi, test_hc_gpio_get_v1_invalid_subcmd)
{
	struct ec_response_gpio_get_v1 response;
	struct ec_params_gpio_get_v1 params = {
		.subcmd = EC_CMD_GPIO_GET,
	};

	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_gpio_get_v1(NULL, &params, &response), NULL);
}

/* EC_CMD_GET_FEATURES */
ZTEST_USER(espi, test_host_command_ec_cmd_get_features)
{
	struct ec_response_get_features response;

	int rv = ec_cmd_get_features(NULL, &response);

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

	system_is_locked_fake.return_val = 1;
	zassert_equal(EC_RES_ACCESS_DENIED, ec_cmd_gpio_set(NULL, &params),
		      NULL);
}

ZTEST(espi, test_hc_gpio_set_invalid_gpio_name)
{
	struct ec_params_gpio_set params = {
		.name = "",
		.val = 0,
	};

	zassert_equal(EC_RES_ERROR, ec_cmd_gpio_set(NULL, &params), NULL);
}

ZTEST_SUITE(espi, drivers_predicate_post_main, NULL, espi_before, espi_after,
	    NULL);
