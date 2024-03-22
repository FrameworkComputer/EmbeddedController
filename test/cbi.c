/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test CBI
 */

#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "gpio.h"
#include "i2c.h"
#include "test_util.h"
#include "util.h"
#include "write_protect.h"

static void test_setup(void)
{
	/* Make sure that write protect is disabled */
	write_protect_set(0);

	cbi_create();
	cbi_write();
}

static void test_teardown(void)
{
}

DECLARE_EC_TEST(test_uint8)
{
	uint8_t d8;
	uint32_t d32;
	uint8_t size;
	const int tag = 0xff;

	/* Set & get uint8_t */
	d8 = 0xa5;
	zassert_equal(cbi_set_board_info(tag, &d8, sizeof(d8)), EC_SUCCESS,
		      NULL);
	size = 1;
	zassert_equal(cbi_get_board_info(tag, &d8, &size), EC_SUCCESS);
	zassert_equal(d8, 0xa5, "0x%x, 0x%x", d8, 0xa5);
	zassert_equal(size, 1, "%x, %x", size, 1);

	/* Size-up */
	d32 = 0x1234abcd;
	zassert_equal(cbi_set_board_info(tag, (void *)&d32, sizeof(d32)),
		      EC_SUCCESS, NULL);
	size = 4;
	zassert_equal(cbi_get_board_info(tag, (void *)&d32, &size), EC_SUCCESS,
		      NULL);
	zassert_equal(d32, 0x1234abcd, "0x%x, 0x%x", d32, 0x1234abcd);
	zassert_equal(size, 4, "%u, %u", size, 4);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_uint32)
{
	uint8_t d8;
	uint32_t d32;
	uint8_t size;
	const int tag = 0xff;

	/* Set & get uint32_t */
	d32 = 0x1234abcd;
	zassert_equal(cbi_set_board_info(tag, (void *)&d32, sizeof(d32)),
		      EC_SUCCESS, NULL);
	size = 4;
	zassert_equal(cbi_get_board_info(tag, (void *)&d32, &size), EC_SUCCESS,
		      NULL);
	zassert_equal(d32, 0x1234abcd, "0x%x, 0x%x", d32, 0x1234abcd);
	zassert_equal(size, 4, "%u, %u", size, 4);

	/* Size-down */
	d8 = 0xa5;
	zassert_equal(cbi_set_board_info(tag, &d8, sizeof(d8)), EC_SUCCESS,
		      NULL);
	size = 1;
	zassert_equal(cbi_get_board_info(tag, &d8, &size), EC_SUCCESS);
	zassert_equal(d8, 0xa5, "0x%x, 0x%x", d8, 0xa5);
	zassert_equal(size, 1, "%u, %u", size, 1);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_string)
{
	const uint8_t string[] = "abcdefghijklmn";
	uint8_t buf[32];
	uint8_t size;
	const int tag = 0xff;

	/* Set & get string */
	zassert_equal(cbi_set_board_info(tag, string, sizeof(string)),
		      EC_SUCCESS, NULL);
	size = sizeof(buf);
	zassert_equal(cbi_get_board_info(tag, buf, &size), EC_SUCCESS);
	zassert_equal(strncmp(buf, string, sizeof(string)), 0);
	/* Size contains null byte */
	/* This should be zassert_equal, but for EC test fmt is always "0x%x"
	 * which will generate compilation error.
	 */
	zassert_true((size_t)size - 1 == strlen(buf), "%zu, %zu",
		     (size_t)size - 1, strlen(buf));

	/* Read buffer too small */
	size = 4;
	zassert_equal(cbi_get_board_info(tag, buf, &size), EC_ERROR_INVAL,
		      NULL);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_not_found)
{
	uint8_t d8;
	const int tag = 0xff;
	uint8_t size;

	size = 1;
	zassert_equal(cbi_get_board_info(tag, &d8, &size), EC_ERROR_UNKNOWN,
		      NULL);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_too_large)
{
	uint8_t buf[CBI_IMAGE_SIZE - 1];
	const int tag = 0xff;

	/* Data too large */
	memset(buf, 0xa5, sizeof(buf));
	zassert_equal(cbi_set_board_info(tag, buf, sizeof(buf)),
		      EC_ERROR_OVERFLOW, NULL);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_all_tags)
{
	uint8_t d8;
	uint32_t d32;
	uint64_t d64;
	const char string[] = "abc";
	uint8_t buf[32];
	uint8_t size;

	/* Populate all data and read out */
	d8 = 0x12;
	zassert_equal(cbi_set_board_info(CBI_TAG_BOARD_VERSION, &d8,
					 sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_OEM_ID, &d8, sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_SKU_ID, &d8, sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_DRAM_PART_NUM, string,
					 sizeof(string)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_OEM_NAME, string,
					 sizeof(string)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_MODEL_ID, &d8, sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_FW_CONFIG, &d8, sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_PCB_SUPPLIER, &d8, sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_SSFC, &d8, sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_REWORK_ID, &d8, sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_FACTORY_CALIBRATION_DATA, &d8,
					 sizeof(d8)),
		      EC_SUCCESS, NULL);
	zassert_equal(cbi_set_board_info(CBI_TAG_COMMON_CONTROL, &d8,
					 sizeof(d8)),
		      EC_SUCCESS, NULL);

	/* Read out all */
	zassert_equal(cbi_get_board_version(&d32), EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);
	zassert_equal(cbi_get_oem_id(&d32), EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);
	zassert_equal(cbi_get_sku_id(&d32), EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);
	size = sizeof(buf);
	zassert_equal(cbi_get_board_info(CBI_TAG_DRAM_PART_NUM, buf, &size),
		      EC_SUCCESS, NULL);
	zassert_equal(strncmp(buf, string, sizeof(string)), 0);
	/* This should be zassert_equal, but for EC test fmt is always "0x%x"
	 * which will generate compilation error.
	 */
	zassert_true((size_t)size - 1 == strlen(buf), "%zu, %zu",
		     (size_t)size - 1, strlen(buf));
	size = sizeof(buf);
	zassert_equal(cbi_get_board_info(CBI_TAG_OEM_NAME, buf, &size),
		      EC_SUCCESS, NULL);
	zassert_equal(strncmp(buf, string, sizeof(string)), 0);
	/* This should be zassert_equal, but for EC test fmt is always "0x%x"
	 * which will generate compilation error.
	 */
	zassert_true((size_t)size - 1 == strlen(buf), "%zu, %zu",
		     (size_t)size - 1, strlen(buf));
	zassert_equal(cbi_get_model_id(&d32), EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);
	zassert_equal(cbi_get_fw_config(&d32), EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);
	zassert_equal(cbi_get_pcb_supplier(&d32), EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);
	zassert_equal(cbi_get_ssfc(&d32), EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);
	zassert_equal(cbi_get_factory_calibration_data(&d32), EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);
	zassert_equal(cbi_get_rework_id(&d64), EC_SUCCESS);
	/* This should be zassert_equal, but for EC test fmt is always "0x%x"
	 * which will generate compilation error.
	 */
	zassert_true((unsigned long long)d64 == (unsigned long long)d8,
		     "0x%llx, 0x%llx", (unsigned long long)d64,
		     (unsigned long long)d8);
	zassert_equal(cbi_get_common_control((union ec_common_control *)&d32),
		      EC_SUCCESS);
	zassert_equal(d32, d8, "0x%x, 0x%x", d32, d8);

	/* Write protect */
	write_protect_set(1);
	zassert_equal(cbi_write(), EC_ERROR_ACCESS_DENIED);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_bad_crc)
{
	uint8_t d8;
	const int tag = 0xff;
	uint8_t size;
	int crc;

	/* Bad CRC */
	d8 = 0xa5;
	zassert_equal(cbi_set_board_info(tag, &d8, sizeof(d8)), EC_SUCCESS,
		      NULL);
	i2c_read8(I2C_PORT_EEPROM, I2C_ADDR_EEPROM_FLAGS,
		  offsetof(struct cbi_header, crc), &crc);
	i2c_write8(I2C_PORT_EEPROM, I2C_ADDR_EEPROM_FLAGS,
		   offsetof(struct cbi_header, crc), ++crc);
	cbi_invalidate_cache();
	size = sizeof(d8);
	zassert_equal(cbi_get_board_info(tag, &d8, &size), EC_ERROR_UNKNOWN,
		      NULL);

	return EC_SUCCESS;
}

TEST_SUITE(test_suite_cbi)
{
	ztest_test_suite(
		test_cbi,
		ztest_unit_test_setup_teardown(test_uint8, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_uint32, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_string, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_not_found, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_too_large, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_all_tags, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_bad_crc, test_setup,
					       test_teardown));
	ztest_run_test_suite(test_cbi);
}
