/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "adc.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

FAKE_VALUE_FUNC(int, adc_read_channel, enum adc_channel);

ZTEST(hc_adc, normal_path)
{
	struct ec_params_adc_read params = {
		.adc_channel = ADC_TEMP_SENSOR_CHARGER,
	};
	struct ec_response_adc_read response;
	uint16_t ret;

	adc_read_channel_fake.return_val = 123;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_ADC_READ, UINT8_C(0), response, params);

	ret = host_command_process(&args);

	zassert_ok(ret, "Host command returned %u", ret);
	zassert_equal(1, adc_read_channel_fake.call_count);
	zassert_equal(123, response.adc_value);
}

ZTEST(hc_adc, bad_ch_number)
{
	struct ec_params_adc_read params = {
		.adc_channel = ADC_CH_COUNT + 1, /* Invalid */
	};
	struct ec_response_adc_read response;
	uint16_t ret;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_ADC_READ, UINT8_C(0), response, params);

	ret = host_command_process(&args);

	zassert_equal(EC_RES_INVALID_PARAM, ret, "Host command returned %u",
		      ret);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	RESET_FAKE(adc_read_channel);
}

ZTEST_SUITE(hc_adc, drivers_predicate_post_main, NULL, reset, reset, NULL);
