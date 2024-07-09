/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "host_command.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#define WP_L_GPIO_PATH NAMED_GPIOS_GPIO_NODE(wp_l)
#define CBI_EEPROM_DEV DEVICE_DT_GET(DT_NODELABEL(cbi_eeprom))

FAKE_VALUE_FUNC(int, eeprom_load, uint8_t, uint8_t *, int);

static int gpio_wp_l_set(int value)
{
	const struct device *wp_l_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(WP_L_GPIO_PATH, gpios));

	return gpio_emul_input_set(wp_l_gpio_dev,
				   DT_GPIO_PIN(WP_L_GPIO_PATH, gpios), value);
}

static int __test_eeprom_load_default_impl(uint8_t offset, uint8_t *data,
					   int len)
{
	int ret = eeprom_read(CBI_EEPROM_DEV, offset, data, len);

	return ret;
}

ZTEST(common_cbi, test_cbi_latch_eeprom_wp)
{
	const struct gpio_dt_spec *wp = GPIO_DT_FROM_ALIAS(gpio_cbi_wp);

	zassert_equal(gpio_emul_output_get(wp->port, wp->pin), 0);

	cbi_latch_eeprom_wp();

	zassert_equal(gpio_emul_output_get(wp->port, wp->pin), 1);
}

ZTEST(common_cbi, test_do_cbi_read__cant_load_head)
{
	enum cbi_data_tag arbitrary_unused_tag = CBI_TAG_SKU_ID;
	uint8_t arbitrary_unused_byte_buffer[100];
	uint8_t unused_data_size;

	/* Force a do_cbi_read() to eeprom */
	cbi_invalidate_cache();

	/* Return arbitrary nonzero value */
	eeprom_load_fake.return_val = 1;
	eeprom_load_fake.custom_fake = NULL;

	zassert_equal(cbi_get_board_info(arbitrary_unused_tag,
					 arbitrary_unused_byte_buffer,
					 &unused_data_size),
		      EC_ERROR_UNKNOWN);
}

ZTEST(common_cbi, test_cbi_set_string__null_str)
{
	struct cbi_data data = { 0 };
	struct cbi_data unused_data = { 0 };
	enum cbi_data_tag arbitrary_valid_tag = CBI_TAG_BOARD_VERSION;

	zassert_equal(cbi_set_string((uint8_t *)&data, arbitrary_valid_tag,
				     NULL),
		      (uint8_t *)&data);

	/* Validate no writes happened */
	zassert_mem_equal(&data, &unused_data, sizeof(data));
}

ZTEST(common_cbi, test_cbi_set_string)
{
	const char arbitrary_str[] = "hello cbi";
	enum cbi_data_tag arbitrary_valid_tag = CBI_TAG_SKU_ID;

	struct cbi_data_wrapper {
		struct cbi_data data;
		uint8_t value_arr[ARRAY_SIZE(arbitrary_str)];
	};
	struct cbi_data_wrapper cbi_data = { 0 };

	/* Set some provided memory then check values */
	uint8_t *addr_byte_after_store = cbi_set_string(
		(uint8_t *)&cbi_data, arbitrary_valid_tag, arbitrary_str);

	zassert_equal(cbi_data.data.tag, arbitrary_valid_tag);
	zassert_equal(cbi_data.data.size, ARRAY_SIZE(arbitrary_str));
	zassert_mem_equal(cbi_data.data.value, arbitrary_str,
			  cbi_data.data.size);

	uint32_t expected_added_memory =
		(ARRAY_SIZE(arbitrary_str) + sizeof(cbi_data.data));

	/* Validate that next address for write was set appropriately */
	zassert_equal_ptr(addr_byte_after_store - expected_added_memory,
			  &cbi_data.data);
}

ZTEST_USER(common_cbi, test_hc_cbi_set_then_get)
{
	const uint8_t data[] = "I love test coverage! <3";

	struct actual_set_params {
		struct ec_params_set_cbi params;
		uint8_t actual_data[ARRAY_SIZE(data)];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.tag = CBI_TAG_SKU_ID,
		/* Force a reload */
		.flag = CBI_SET_INIT,
		.size = ARRAY_SIZE(data),
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_set_params);

	memcpy(hc_set_params.params.data, data, ARRAY_SIZE(data));

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);

	zassert_ok(host_command_process(&set_args));

	/* Now verify our write by invoking a get host command */

	struct ec_params_get_cbi hc_get_params = {
		.flag = CBI_GET_RELOAD,
		.tag = hc_set_params.params.tag,
	};

	struct test_ec_params_get_cbi_response {
		uint8_t data[ARRAY_SIZE(data)];
	};
	struct test_ec_params_get_cbi_response hc_get_response;
	struct host_cmd_handler_args get_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_CROS_BOARD_INFO, 0, hc_get_response, hc_get_params);

	zassert_ok(host_command_process(&get_args));
	zassert_mem_equal(hc_get_response.data, hc_set_params.actual_data,
			  hc_set_params.params.size);
}

ZTEST_USER(common_cbi, test_hc_cbi_set__bad_size)
{
	const char data[] = "hello";

	struct actual_set_params {
		struct ec_params_set_cbi params;
		/* We want less data than we need for our size */
		uint8_t actual_data[0];
	};
	struct actual_set_params hc_set_params = {
		.params = {
		.tag = CBI_TAG_SKU_ID,
		/* Force a reload */
		.flag = CBI_SET_INIT,
		.size = ARRAY_SIZE(data),
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_set_params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}

ZTEST_USER(common_cbi, test_hc_cbi_set_then_get__with_too_small_response)
{
	const uint8_t data[] = "I'm way too big of a payload for you!";

	struct actual_set_params {
		struct ec_params_set_cbi params;
		uint8_t actual_data[ARRAY_SIZE(data)];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.tag = CBI_TAG_SKU_ID,
		/* Force a reload */
		.flag = CBI_SET_INIT,
		.size = ARRAY_SIZE(data),
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_set_params);

	memcpy(hc_set_params.params.data, data, ARRAY_SIZE(data));

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);

	zassert_ok(host_command_process(&set_args));

	/* Now verify our write by invoking a get host command */

	struct ec_params_get_cbi hc_get_params = {
		.flag = CBI_GET_RELOAD,
		.tag = hc_set_params.params.tag,
	};

	struct test_ec_params_get_cbi_response {
		/*
		 * Want want less space than we need to retrieve cbi data, by
		 * allocating an array of size zero, we're implicitly setting
		 * the response_max value of the host command to be zero. So the
		 * host command will fail because it the EC knows it doesn't
		 * have enough response space to actually fetch the data for the
		 * host.
		 */
		uint8_t data[0];
	};
	struct test_ec_params_get_cbi_response hc_get_response;
	struct host_cmd_handler_args get_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_CROS_BOARD_INFO, 0, hc_get_response, hc_get_params);

	zassert_equal(host_command_process(&get_args), EC_RES_INVALID_PARAM);
}

ZTEST_USER(common_cbi, test_hc_cbi_bin_write_then_read)
{
	/*
	 * cbi_bin commands will do a validity check on the header.
	 * This data allows the cbi to pass the validity check.
	 */
	const uint8_t data[] = {
		0x43, 0x42, 0x49, 0x96, 0x00, 0x00, 0x30, 0x00
	};

	struct actual_set_params {
		struct ec_params_set_cbi_bin params;
		uint8_t actual_data[ARRAY_SIZE(data)];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.offset = 0,
		.size = ARRAY_SIZE(data),
		.flags = EC_CBI_BIN_BUFFER_CLEAR | EC_CBI_BIN_BUFFER_WRITE,
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_CBI_BIN_WRITE, 0, hc_set_params);

	memcpy(hc_set_params.params.data, data, ARRAY_SIZE(data));

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);

	zassert_ok(host_command_process(&set_args));

	struct ec_params_get_cbi_bin hc_get_params = {
		.offset = 0,
		.size = ARRAY_SIZE(data),
	};

	struct test_ec_params_get_cbi_response {
		uint8_t data[ARRAY_SIZE(data)];
	};
	struct test_ec_params_get_cbi_response hc_get_response;
	struct host_cmd_handler_args get_args = BUILD_HOST_COMMAND(
		EC_CMD_CBI_BIN_READ, 0, hc_get_response, hc_get_params);

	zassert_ok(host_command_process(&get_args));

	zassert_mem_equal(hc_get_response.data, hc_set_params.params.data,
			  hc_set_params.params.size);
}

ZTEST_USER(common_cbi, test_hc_cbi_bin_read_bad_param)
{
	/* request exceeds cbi buffer size*/
	struct ec_params_get_cbi_bin hc_get_params = {
		.offset = 0,
		.size = CBI_IMAGE_SIZE + 1,
	};

	struct test_ec_params_get_cbi_response_small {
		uint8_t data[CBI_IMAGE_SIZE + 1];
	};
	struct test_ec_params_get_cbi_response_small hc_get_response_small;
	struct host_cmd_handler_args get_args_1 = BUILD_HOST_COMMAND(
		EC_CMD_CBI_BIN_READ, 0, hc_get_response_small, hc_get_params);

	zassert_equal(host_command_process(&get_args_1), EC_RES_INVALID_PARAM);

	/* offset too big */
	hc_get_params.offset = CBI_IMAGE_SIZE + 1;
	hc_get_params.size = 64;

	struct test_ec_params_get_cbi_response {
		uint8_t data[64];
	};
	struct test_ec_params_get_cbi_response hc_get_response;
	struct host_cmd_handler_args get_args_2 = BUILD_HOST_COMMAND(
		EC_CMD_CBI_BIN_READ, 0, hc_get_response, hc_get_params);

	zassert_equal(host_command_process(&get_args_2), EC_RES_INVALID_PARAM);

	/* read area too big */
	hc_get_params.offset = CBI_IMAGE_SIZE - 1;

	struct host_cmd_handler_args get_args_3 = BUILD_HOST_COMMAND(
		EC_CMD_CBI_BIN_READ, 0, hc_get_response, hc_get_params);

	zassert_equal(host_command_process(&get_args_3), EC_RES_INVALID_PARAM);
}

ZTEST_USER(common_cbi, test_hc_cbi_bin_write_bad_cbi)
{
	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);

	/* data fails cbi magic checker */
	const uint8_t data[] = {
		0x43, 0x42, 0x00, 0x96, 0x00, 0x00, 0x30, 0x00
	};

	struct actual_set_params {
		struct ec_params_set_cbi_bin params;
		uint8_t actual_data[ARRAY_SIZE(data)];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.offset = 0,
		.size = ARRAY_SIZE(data),
		.flags = EC_CBI_BIN_BUFFER_CLEAR | EC_CBI_BIN_BUFFER_WRITE,
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_CBI_BIN_WRITE, 0, hc_set_params);

	memcpy(hc_set_params.params.data, data, ARRAY_SIZE(data));

	zassert_equal(host_command_process(&set_args), EC_RES_ERROR);

	/* fails cbi crc */
	hc_set_params.params.data[2] = 0x49;
	hc_set_params.params.data[3] = 0x00;

	zassert_equal(host_command_process(&set_args), EC_RES_ERROR);

	/* fails cbi version */
	hc_set_params.params.data[3] = 0x96;
	hc_set_params.params.data[5] = 0x96;

	zassert_equal(host_command_process(&set_args), EC_RES_ERROR);

	/* fails cbi size */
	hc_set_params.params.data[5] = 0x00;
	hc_set_params.params.data[7] = 0x30;

	zassert_equal(host_command_process(&set_args), EC_RES_ERROR);
}

ZTEST_USER(common_cbi, test_hc_cbi_bin_write_bad_param)
{
	struct actual_set_params {
		struct ec_params_set_cbi_bin params;
		uint8_t actual_data[32];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.offset = 0,
		.size = 32,
		.flags = 0,
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_CBI_BIN_WRITE, 0, hc_set_params);

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);

	/* area too big */
	hc_set_params.params.size = 32;
	hc_set_params.params.offset = CBI_IMAGE_SIZE - 1;
	zassert_equal(host_command_process(&set_args), EC_RES_INVALID_PARAM);

	/*
	 * offset too big
	 * any command with offset too big will also have area too big,
	 * but the detailed error log will have a different message
	 */
	hc_set_params.params.offset = CBI_IMAGE_SIZE + 1;
	zassert_equal(host_command_process(&set_args), EC_RES_INVALID_PARAM);
}

static void test_common_cbi_before_after(void *test_data)
{
	RESET_FAKE(eeprom_load);
	eeprom_load_fake.custom_fake = __test_eeprom_load_default_impl;

	cbi_create();
}

ZTEST_SUITE(common_cbi, drivers_predicate_post_main, NULL,
	    test_common_cbi_before_after, test_common_cbi_before_after, NULL);
