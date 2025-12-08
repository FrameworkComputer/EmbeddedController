/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "host_command.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test_util.h"

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
		.flag = 0,
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
	zassert_equal(get_args.response_size, sizeof(hc_get_response));
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
		.flag = 0,
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
		.flag = 0,
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
	zassert_equal(get_args.response_size, sizeof(hc_get_response));

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

ZTEST_USER(common_cbi, test_blank_then_init)
{
	const uint8_t board_id = 42;
	const uint8_t oem_id = 99;
	const uint8_t oem_name[] = "Name";

	struct actual_set_params {
		struct ec_params_set_cbi params;
		uint8_t actual_data[32];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.tag = CBI_TAG_BOARD_VERSION,
		.flag = CBI_SET_INIT,
		.size = sizeof(board_id),
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_set_params);

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);
	zassert_ok(cbi_clear(), NULL);

	/* Write the board version. */
	memcpy(hc_set_params.params.data, &board_id, sizeof(board_id));
	zassert_ok(host_command_process(&set_args));

	/* Write the oem id. */
	hc_set_params.params.tag = CBI_TAG_OEM_ID;
	hc_set_params.params.flag = 0;
	hc_set_params.params.size = sizeof(oem_id);
	memcpy(hc_set_params.params.data, &oem_id, sizeof(oem_id));
	zassert_ok(host_command_process(&set_args));

	/* Write oem name */
	hc_set_params.params.tag = CBI_TAG_OEM_NAME;
	hc_set_params.params.flag = 0;
	hc_set_params.params.size = ARRAY_SIZE(oem_name);
	memcpy(hc_set_params.params.data, oem_name, ARRAY_SIZE(oem_name));
	zassert_ok(host_command_process(&set_args));

	/* Now verify our writes by invoking get host commands */

	struct ec_params_get_cbi hc_get_params = {
		.flag = CBI_GET_RELOAD,
		.tag = CBI_TAG_BOARD_VERSION,
	};

	struct test_ec_params_get_cbi_response {
		uint8_t data[32];
	};
	struct test_ec_params_get_cbi_response hc_get_response;
	struct host_cmd_handler_args get_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_CROS_BOARD_INFO, 0, hc_get_response, hc_get_params);

	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_args.response_size, sizeof(board_id));
	zassert_equal(hc_get_response.data[0], board_id);

	hc_get_params.flag = 0;
	hc_get_params.tag = CBI_TAG_OEM_ID;

	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_args.response_size, sizeof(oem_id));
	zassert_equal(hc_get_response.data[0], oem_id);

	hc_get_params.flag = 0;
	hc_get_params.tag = CBI_TAG_OEM_NAME;

	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_args.response_size, ARRAY_SIZE(oem_name));
	zassert_mem_equal(hc_get_response.data, oem_name, ARRAY_SIZE(oem_name));
}

ZTEST_USER(common_cbi, test_init_fails_when_locked)
{
	const uint8_t oem_name[] = "Name";

	struct actual_set_params {
		struct ec_params_set_cbi params;
		uint8_t actual_data[32];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.tag = CBI_TAG_OEM_NAME,
		.flag = CBI_SET_INIT,
		.size = ARRAY_SIZE(oem_name),
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_set_params);

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);

	/* Write the oem name with init. */
	memcpy(hc_set_params.params.data, oem_name, ARRAY_SIZE(oem_name));
#ifdef CONFIG_SYSTEM_UNLOCKED
	zassert_ok(host_command_process(&set_args));

	/* Now verify our that board id & oem id are blank, and oem name is set
	 */

	struct ec_params_get_cbi hc_get_params = {
		.flag = CBI_GET_RELOAD,
		.tag = CBI_TAG_BOARD_VERSION,
	};

	struct test_ec_params_get_cbi_response {
		uint8_t data[32];
	};
	struct test_ec_params_get_cbi_response hc_get_response;
	struct host_cmd_handler_args get_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_CROS_BOARD_INFO, 0, hc_get_response, hc_get_params);

	zassert_not_ok(host_command_process(&get_args));

	hc_get_params.flag = 0;
	hc_get_params.tag = CBI_TAG_OEM_ID;

	zassert_not_ok(host_command_process(&get_args));

	hc_get_params.flag = 0;
	hc_get_params.tag = CBI_TAG_OEM_NAME;

	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_args.response_size, ARRAY_SIZE(oem_name));
	zassert_mem_equal(hc_get_response.data, oem_name, ARRAY_SIZE(oem_name));
#else
	zassert_not_ok(host_command_process(&set_args));
#endif /* CONFIG_SYSTEM_UNLOCKED */
}

ZTEST_USER(common_cbi, test_model_id_set)
{
	uint32_t model_id = 234;
	int rv;

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);
	zassert_ok(cbi_clear());

	/* Set model ID directly */
	rv = cbi_set_model_id(model_id);
	zassert_equal(rv, EC_SUCCESS);
}

ZTEST_USER(common_cbi, test_model_id_hc_set_get)
{
	uint32_t model_id = 234;
	uint32_t model_id_read;

	struct actual_set_params {
		struct ec_params_set_cbi params;
		uint8_t actual_data[sizeof(model_id)];
	};

	struct actual_set_params hc_set_params = {
	.params = {
		.tag = CBI_TAG_MODEL_ID,
		.flag = CBI_SET_INIT,  /* This is crucial! */
		.size = sizeof(model_id),
		},
	};

	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_set_params);

	memcpy(hc_set_params.params.data, &model_id, sizeof(model_id));

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);
	zassert_ok(cbi_clear());

	/* Set model ID via host command */
	zassert_ok(host_command_process(&set_args));

	/* Verify model ID was set correctly */
	zassert_ok(cbi_get_model_id(&model_id_read));
	zassert_equal(model_id_read, model_id);
}

ZTEST_USER(common_cbi, test_model_id_set_fail_bad_magic)
{
	uint32_t model_id = 456;

	/* Turn off write-protect */
	gpio_wp_l_set(1);

	/* First create valid CBI */
	zassert_ok(cbi_clear());
	zassert_ok(cbi_set_model_id(model_id));

	/* Now corrupt the magic in storage to make do_cbi_read() fail */
	uint8_t bad_magic[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

	/* Write bad magic to EEPROM offset 0 */
	const struct device *eeprom_dev = CBI_EEPROM_DEV;
	zassert_ok(eeprom_write(eeprom_dev, 0, bad_magic, sizeof(bad_magic)));

	/* Invalidate cache to force read from storage */
	cbi_invalidate_cache();

	/*
	 * Try to set model ID.do_cbi_read() will fail due to bad magic.
	 * But cbi_create() will be called, so function should succeed.
	 */
	int rv = cbi_set_model_id(model_id + 1);
	zassert_ok(rv);
}

ZTEST_USER(common_cbi, test_board_id_fails_when_set)
{
	uint8_t board_id = 42;

	struct actual_set_params {
		struct ec_params_set_cbi params;
		uint8_t actual_data[32];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.tag = CBI_TAG_BOARD_VERSION,
		.flag = CBI_SET_INIT,
		.size = sizeof(board_id),
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_set_params);

	/* Turn off write-protect so we can actually write */
	gpio_wp_l_set(1);
	zassert_ok(cbi_clear(), NULL);

	/* Write the board version. */
	memcpy(hc_set_params.params.data, &board_id, sizeof(board_id));
	zassert_ok(host_command_process(&set_args));

	/* Now verify our writes by invoking get host commands */

	struct ec_params_get_cbi hc_get_params = {
		.flag = CBI_GET_RELOAD,
		.tag = CBI_TAG_BOARD_VERSION,
	};

	struct test_ec_params_get_cbi_response {
		uint8_t data[32];
	};
	struct test_ec_params_get_cbi_response hc_get_response;
	struct host_cmd_handler_args get_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_CROS_BOARD_INFO, 0, hc_get_response, hc_get_params);

	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_args.response_size, sizeof(board_id));
	zassert_equal(hc_get_response.data[0], board_id);

	/* Write it again without init (should fail if locked). */
	board_id = 43;
	memcpy(hc_set_params.params.data, &board_id, sizeof(board_id));
	hc_set_params.params.flag = 0;
#ifdef CONFIG_SYSTEM_UNLOCKED
	zassert_ok(host_command_process(&set_args));
#else
	zassert_not_ok(host_command_process(&set_args));
	board_id = 42;
#endif /* CONFIG_SYSTEM_UNLOCKED */

	/* verify again */
	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_args.response_size, sizeof(board_id));
	zassert_equal(hc_get_response.data[0], board_id);
}

ZTEST_USER(common_cbi, test_cbi_get_ufsc__read_write)
{
	const struct cbi_ufsc ufsc_to_write = {
		.data = { 0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00 }
	};
	struct actual_set_params {
		struct ec_params_set_cbi params;
		struct cbi_ufsc data;
	};
	struct actual_set_params hc_params = {
		.params = {
			.tag = CBI_TAG_UFSC,
			.size = sizeof(ufsc_to_write),
		},
		.data = ufsc_to_write,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_params);
	struct cbi_ufsc ufsc_read;

	/* Turn off write-protect so we can actually write. */
	gpio_wp_l_set(1);

	zassert_ok(host_command_process(&args), "Failed to set UFSC data");

	/* Invalidate cache to force a read from storage. */
	cbi_invalidate_cache();

	zassert_ok(cbi_get_ufsc(&ufsc_read), "cbi_get_ufsc failed");
	zassert_mem_equal(&ufsc_to_write, &ufsc_read, sizeof(struct cbi_ufsc),
			  "Read UFSC data does not match written data");

	/* Test the console command output for the written data. */
	const char expected_scan[] = "4433221188776655ccbbaa9900ffeedd";
	char ufsc_scan[sizeof(expected_scan)];
	BUILD_ASSERT(sizeof(expected_scan) - 1 == 32);

	SCAN_CONSOLE_LINE("cbi", EC_SUCCESS, "UFSC:", 1, "UFSC: %32s",
			  ufsc_scan);
	zassert_equal(strcmp(ufsc_scan, expected_scan), 0,
		      "Console print of UFSC value does not match. "
		      "Expected '%s', got '%s'",
		      expected_scan, ufsc_scan);
}

ZTEST_USER(common_cbi, test_cbi_get_ufsc__not_found)
{
	struct cbi_ufsc ufsc_read;
	int rv;

	/* Clear CBI to ensure the tag is not present. */
	gpio_wp_l_set(1);
	zassert_ok(cbi_clear(), "cbi_clear failed");

	rv = cbi_get_ufsc(&ufsc_read);
	zassert_equal(rv, EC_ERROR_UNKNOWN,
		      "Expected EC_ERROR_UNKNOWN for missing tag, but got %d",
		      rv);
}

ZTEST_USER(common_cbi, test_cros_cbi_ufsc_match)
{
	struct cbi_ufsc ufsc_to_write = { 0 };
	struct actual_set_params {
		struct ec_params_set_cbi params;
		struct cbi_ufsc data;
	};
	struct actual_set_params hc_params = {
		.params = {
			.tag = CBI_TAG_UFSC,
			.size = sizeof(ufsc_to_write),
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_params);

	/*
	 * data[0]: Set test-field-1 (start=0, size=2) to value 2 and
	 * test-field-2 (start=2, size=3) to value 5.
	 * Value = (2 << 0) | (5 << 2) = 2 | 20 = 22 (0x16).
	 */
	ufsc_to_write.data[0] = 0x16;
	/*
	 * data[3]: Set test-field-3 (start=98, size=1) to value 1.
	 * Bit offset in data[3] is 98 % 32 = 2. Value = BIT(2).
	 */
	ufsc_to_write.data[3] = BIT(2);
	hc_params.data = ufsc_to_write;

	gpio_wp_l_set(1);

	zassert_ok(host_command_process(&args), "Failed to set UFSC data");

	/* Re-initialize UFSC driver to pick up new CBI value */
	cros_cbi_ufsc_init();

	/* Field 1 was set to 2, which corresponds to value_b */
	zassert_true(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_b))));
	zassert_false(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_a))));

	/* Field 2 was set to 5, which corresponds to value_c */
	zassert_true(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_c))));

	/* Field 3 was set to 1, which corresponds to value_d */
	zassert_true(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_d))));
	zassert_false(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_e))));
}

ZTEST_USER(common_cbi, test_cros_cbi_ufsc_default)
{
	/* Clear CBI so that driver uses defaults */
	gpio_wp_l_set(1);
	zassert_ok(cbi_clear(), "cbi_clear failed");
	cros_cbi_ufsc_init();

	/* Field 1's default is value_a (value=1) */
	zassert_true(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_a))));
	zassert_false(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_b))));

	/* Field 3's default is value_e (value=0) */
	zassert_true(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_e))));
	zassert_false(cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(value_d))));
}

static void test_common_cbi_before_after(void *test_data)
{
	RESET_FAKE(eeprom_load);
	eeprom_load_fake.custom_fake = __test_eeprom_load_default_impl;

	cbi_create();
}

ZTEST_SUITE(common_cbi, drivers_predicate_post_main, NULL,
	    test_common_cbi_before_after, test_common_cbi_before_after, NULL);
