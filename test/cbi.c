/* Copyright 2020 The Chromium OS Authors. All rights reserved.
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

void before_test(void)
{
	cbi_create();
	cbi_write();
}

static int test_uint8(void)
{
	uint8_t d8;
	uint32_t d32;
	uint8_t size;
	const int tag = 0xff;

	/* Set & get uint8_t */
	d8 = 0xa5;
	TEST_ASSERT(cbi_set_board_info(tag, &d8, sizeof(d8)) == EC_SUCCESS);
	size = 1;
	TEST_ASSERT(cbi_get_board_info(tag, &d8, &size) == EC_SUCCESS);
	TEST_EQ(d8, 0xa5, "0x%x");
	TEST_EQ(size, 1, "%x");

	/* Size-up */
	d32 = 0x1234abcd;
	TEST_ASSERT(cbi_set_board_info(tag, (void *)&d32, sizeof(d32))
		    == EC_SUCCESS);
	size = 4;
	TEST_ASSERT(cbi_get_board_info(tag, (void *)&d32, &size) == EC_SUCCESS);
	TEST_EQ(d32, 0x1234abcd, "0x%x");
	TEST_EQ(size, 4, "%u");

	return EC_SUCCESS;
}

static int test_uint32(void)
{
	uint8_t d8;
	uint32_t d32;
	uint8_t size;
	const int tag = 0xff;

	/* Set & get uint32_t */
	d32 = 0x1234abcd;
	TEST_ASSERT(cbi_set_board_info(tag, (void *)&d32, sizeof(d32))
			== EC_SUCCESS);
	size = 4;
	TEST_ASSERT(cbi_get_board_info(tag, (void *)&d32, &size) == EC_SUCCESS);
	TEST_EQ(d32, 0x1234abcd, "0x%x");
	TEST_EQ(size, 4, "%u");

	/* Size-down */
	d8 = 0xa5;
	TEST_ASSERT(cbi_set_board_info(tag, &d8, sizeof(d8)) == EC_SUCCESS);
	size = 1;
	TEST_ASSERT(cbi_get_board_info(tag, &d8, &size) == EC_SUCCESS);
	TEST_EQ(d8, 0xa5, "0x%x");
	TEST_EQ(size, 1, "%u");

	return EC_SUCCESS;
}

static int test_string(void)
{
	const uint8_t string[] = "abcdefghijklmn";
	uint8_t buf[32];
	uint8_t size;
	const int tag = 0xff;

	/* Set & get string */
	TEST_ASSERT(cbi_set_board_info(tag, string, sizeof(string))
		    == EC_SUCCESS);
	size = sizeof(buf);
	TEST_ASSERT(cbi_get_board_info(tag, buf, &size) == EC_SUCCESS);
	TEST_ASSERT(strncmp(buf, string, sizeof(string)) == 0);
	/* Size contains null byte */
	TEST_EQ((size_t)size - 1, strlen(buf), "%zu");

	/* Read buffer too small */
	size = 4;
	TEST_ASSERT(cbi_get_board_info(tag, buf, &size) == EC_ERROR_INVAL);

	return EC_SUCCESS;
}

static int test_not_found(void)
{
	uint8_t d8;
	const int tag = 0xff;
	uint8_t size;

	size = 1;
	TEST_ASSERT(cbi_get_board_info(tag, &d8, &size) == EC_ERROR_UNKNOWN);

	return EC_SUCCESS;
}

static int test_too_large(void)
{
	uint8_t buf[CBI_EEPROM_SIZE-1];
	const int tag = 0xff;

	/* Data too large */
	memset(buf, 0xa5, sizeof(buf));
	TEST_ASSERT(cbi_set_board_info(tag, buf, sizeof(buf))
		    == EC_ERROR_OVERFLOW);

	return EC_SUCCESS;
}

static int test_all_tags(void)
{
	uint8_t d8;
	uint32_t d32;
	const char string[] = "abc";
	uint8_t buf[32];
	uint8_t size;
	int count = 0;

	/* Populate all data and read out */
	d8 = 0x12;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_BOARD_VERSION, &d8, sizeof(d8))
		    == EC_SUCCESS);
	count++;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_OEM_ID, &d8, sizeof(d8))
		    == EC_SUCCESS);
	count++;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_SKU_ID, &d8, sizeof(d8))
		    == EC_SUCCESS);
	count++;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_DRAM_PART_NUM,
				       string, sizeof(string))
		    == EC_SUCCESS);
	count++;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_OEM_NAME,
				       string, sizeof(string))
		    == EC_SUCCESS);
	count++;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_MODEL_ID, &d8, sizeof(d8))
		    == EC_SUCCESS);
	count++;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_FW_CONFIG, &d8, sizeof(d8))
		    == EC_SUCCESS);
	count++;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_PCB_SUPPLIER, &d8, sizeof(d8))
		    == EC_SUCCESS);
	count++;
	TEST_ASSERT(cbi_set_board_info(CBI_TAG_SSFC, &d8, sizeof(d8))
		    == EC_SUCCESS);
	count++;

	/* Read out all */
	TEST_ASSERT(cbi_get_board_version(&d32) == EC_SUCCESS);
	TEST_EQ(d32, d8, "0x%x");
	TEST_ASSERT(cbi_get_oem_id(&d32) == EC_SUCCESS);
	TEST_EQ(d32, d8, "0x%x");
	TEST_ASSERT(cbi_get_sku_id(&d32) == EC_SUCCESS);
	TEST_EQ(d32, d8, "0x%x");
	size = sizeof(buf);
	TEST_ASSERT(cbi_get_board_info(CBI_TAG_DRAM_PART_NUM, buf, &size)
		    == EC_SUCCESS);
	TEST_ASSERT(strncmp(buf, string, sizeof(string)) == 0);
	TEST_EQ((size_t)size - 1, strlen(buf), "%zu");
	size = sizeof(buf);
	TEST_ASSERT(cbi_get_board_info(CBI_TAG_OEM_NAME, buf, &size)
		    == EC_SUCCESS);
	TEST_ASSERT(strncmp(buf, string, sizeof(string)) == 0);
	TEST_EQ((size_t)size - 1, strlen(buf), "%zu");
	TEST_ASSERT(cbi_get_model_id(&d32) == EC_SUCCESS);
	TEST_EQ(d32, d8, "0x%x");
	TEST_ASSERT(cbi_get_fw_config(&d32) == EC_SUCCESS);
	TEST_EQ(d32, d8, "0x%x");
	TEST_ASSERT(cbi_get_pcb_supplier(&d32) == EC_SUCCESS);
	TEST_EQ(d32, d8, "0x%x");
	TEST_ASSERT(cbi_get_ssfc(&d32) == EC_SUCCESS);
	TEST_EQ(d32, d8, "0x%x");

	/* Fail if a (new) tag is missing from the unit test. */
	TEST_EQ(count, CBI_TAG_COUNT, "%d");

	/* Write protect */
	gpio_set_level(GPIO_WP, 1);
	TEST_ASSERT(cbi_write() == EC_ERROR_ACCESS_DENIED);

	return EC_SUCCESS;
}

static int test_bad_crc(void)
{
	uint8_t d8;
	const int tag = 0xff;
	uint8_t size;
	int crc;

	/* Bad CRC */
	d8 = 0xa5;
	TEST_ASSERT(cbi_set_board_info(tag, &d8, sizeof(d8)) == EC_SUCCESS);
	i2c_read8(I2C_PORT_EEPROM, I2C_ADDR_EEPROM_FLAGS,
		   offsetof(struct cbi_header, crc), &crc);
	i2c_write8(I2C_PORT_EEPROM, I2C_ADDR_EEPROM_FLAGS,
		   offsetof(struct cbi_header, crc), ++crc);
	cbi_invalidate_cache();
	size = sizeof(d8);
	TEST_ASSERT(cbi_get_board_info(tag, &d8, &size) == EC_ERROR_UNKNOWN);

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_uint8);
	RUN_TEST(test_uint32);
	RUN_TEST(test_string);
	RUN_TEST(test_not_found);
	RUN_TEST(test_too_large);
	RUN_TEST(test_all_tags);
	RUN_TEST(test_bad_crc);

	test_print_result();
}
