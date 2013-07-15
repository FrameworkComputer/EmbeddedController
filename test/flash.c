/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console commands to trigger flash host commands */

#include "console.h"
#include "ec_commands.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int last_write_offset;
static int last_write_size;
static char last_write_data[64];

static int last_erase_offset;
static int last_erase_size;

static int mock_wp = -1;

static int mock_flash_op_fail = EC_SUCCESS;

const char *testdata = "TestData0000000"; /* 16 bytes */

#define TEST_STATE_CLEAN_UP    (1 << 0)
#define TEST_STATE_STEP_2      (1 << 1)
#define TEST_STATE_STEP_3      (1 << 2)
#define TEST_STATE_BOOT_WP_ON  (1 << 3)
#define TEST_STATE_PASSED      (1 << 4)
#define TEST_STATE_FAILED      (1 << 5)

#define CLEAN_UP_FLAG_PASSED TEST_STATE_PASSED
#define CLEAN_UP_FLAG_FAILED TEST_STATE_FAILED

/*****************************************************************************/
/* Emulator-only mock functions */
#ifdef EMU_BUILD
static int mock_is_running_img;

int system_unsafe_to_overwrite(uint32_t offset, uint32_t size)
{
	return mock_is_running_img;
}
#endif

/*****************************************************************************/
/* Mock functions */
void host_send_response(struct host_cmd_handler_args *args)
{
	/* Do nothing */
}

int flash_write(int offset, int size, const char *data)
{
	if (mock_flash_op_fail != EC_SUCCESS)
		return mock_flash_op_fail;
	last_write_offset = offset;
	last_write_size = size;
	memcpy(last_write_data, data, size);
	return EC_SUCCESS;
}

int flash_erase(int offset, int size)
{
	if (mock_flash_op_fail != EC_SUCCESS)
		return mock_flash_op_fail;
	last_erase_offset = offset;
	last_erase_size = size;
	return EC_SUCCESS;
}

int gpio_get_level(enum gpio_signal signal)
{
	const char *name = gpio_list[signal].name;

	if (mock_wp == -1)
		mock_wp = !!(system_get_scratchpad() & TEST_STATE_BOOT_WP_ON);

	if (strcasecmp(name, "WP_L") == 0)
		return !mock_wp;
	if (strcasecmp(name, "WP") == 0)
		return mock_wp;

	/* Signal other than write protect. Just return 0. */
	return 0;
}

/*****************************************************************************/
/* Test utilities */

static void begin_verify(void)
{
	last_write_offset = -1;
	last_write_size = -1;
	last_write_data[0] = '\0';
	last_erase_offset = -1;
	last_erase_size = -1;
}

static int verify_write(int offset, int size, const char *data)
{
	int i;

	if (offset != last_write_offset || size != last_write_size)
		return EC_ERROR_UNKNOWN;
	for (i = 0; i < size; ++i)
		if (data[i] != last_write_data[i])
			return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}


#define VERIFY_NO_WRITE(off, sz, d) \
	do { \
		begin_verify(); \
		TEST_ASSERT(host_command_write(off, sz, d) != EC_SUCCESS); \
		TEST_ASSERT(last_write_offset == -1 && last_write_size == -1); \
	} while (0)

#define VERIFY_NO_ERASE(off, sz) \
	do { \
		begin_verify(); \
		TEST_ASSERT(host_command_erase(off, sz) != EC_SUCCESS); \
		TEST_ASSERT(last_erase_offset == -1 && last_erase_size == -1); \
	} while (0)

#define VERIFY_WRITE(off, sz, d) \
	do { \
		begin_verify(); \
		TEST_ASSERT(host_command_write(off, sz, d) == EC_SUCCESS); \
		TEST_ASSERT(verify_write(off, sz, d) == EC_SUCCESS); \
	} while (0)

#define VERIFY_ERASE(off, sz) \
	do { \
		begin_verify(); \
		TEST_ASSERT(host_command_erase(off, sz) == EC_SUCCESS); \
		TEST_ASSERT(last_erase_offset == off && \
			    last_erase_size == sz); \
	} while (0)

#define SET_WP_FLAGS(m, f) \
	TEST_ASSERT(host_command_protect(m, ((f) ? m : 0), \
				NULL, NULL, NULL) == EC_RES_SUCCESS)

#define ASSERT_WP_FLAGS(f) \
	do { \
		uint32_t flags; \
		TEST_ASSERT(host_command_protect(0, 0, &flags, NULL, NULL) == \
			    EC_RES_SUCCESS); \
		TEST_ASSERT(flags & (f)); \
	} while (0)

#define ASSERT_WP_NO_FLAGS(f) \
	do { \
		uint32_t flags; \
		TEST_ASSERT(host_command_protect(0, 0, &flags, NULL, NULL) == \
			    EC_RES_SUCCESS); \
		TEST_ASSERT((flags & (f)) == 0); \
	} while (0)

#define VERIFY_REGION_INFO(r, o, s) \
	do { \
		uint32_t offset, size; \
		TEST_ASSERT(host_command_region_info(r, &offset, &size) == \
			    EC_RES_SUCCESS); \
		TEST_ASSERT(offset == (o)); \
		TEST_ASSERT(size == (s)); \
	} while (0)

int host_command_read(int offset, int size, char *out)
{
	struct ec_params_flash_read params;

	params.offset = offset;
	params.size = size;

	return test_send_host_command(EC_CMD_FLASH_READ, 0, &params,
				      sizeof(params), out, size);
}

int host_command_write(int offset, int size, const char *data)
{
	uint8_t buf[256];
	struct ec_params_flash_write *params =
		(struct ec_params_flash_write *)buf;

	params->offset = offset;
	params->size = size;
	memcpy(params + 1, data, size);

	return test_send_host_command(EC_CMD_FLASH_WRITE, EC_VER_FLASH_WRITE,
				      buf, size + sizeof(*params), NULL, 0);
}

int host_command_erase(int offset, int size)
{
	struct ec_params_flash_write params;

	params.offset = offset;
	params.size = size;

	return test_send_host_command(EC_CMD_FLASH_ERASE, 0, &params,
				      sizeof(params), NULL, 0);
}

int host_command_protect(uint32_t mask, uint32_t flags,
			 uint32_t *flags_out, uint32_t *valid_out,
			 uint32_t *writable_out)
{
	struct ec_params_flash_protect params;
	struct ec_response_flash_protect resp;
	int res;

	params.mask = mask;
	params.flags = flags;

	res = test_send_host_command(EC_CMD_FLASH_PROTECT, 1, &params,
				     sizeof(params), &resp, sizeof(resp));

	if (res == EC_RES_SUCCESS) {
		if (flags_out)
			*flags_out = resp.flags;
		if (valid_out)
			*valid_out = resp.valid_flags;
		if (writable_out)
			*writable_out = resp.writable_flags;
	}

	return res;
}

int host_command_region_info(enum ec_flash_region reg, uint32_t *offset,
				 uint32_t *size)
{
	struct ec_params_flash_region_info params;
	struct ec_response_flash_region_info resp;
	int res;

	params.region = reg;

	res = test_send_host_command(EC_CMD_FLASH_REGION_INFO, 1, &params,
				     sizeof(params), &resp, sizeof(resp));

	*offset = resp.offset;
	*size = resp.size;

	return res;
}

/*****************************************************************************/
/* Tests */
static int test_read(void)
{
	char buf[16];

#ifdef EMU_BUILD
	int i;
	/* Fill in some numbers so they are not all 0xff */
	for (i = 0; i < sizeof(buf); ++i)
		__host_flash[i] = i * i + i;
#endif

	/* The first few bytes in the flash should always contain some code */
	TEST_ASSERT(!flash_is_erased(0, sizeof(buf)));

	TEST_ASSERT(host_command_read(0, sizeof(buf), buf) == EC_RES_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(buf, (char *)CONFIG_FLASH_BASE, sizeof(buf));

	return EC_SUCCESS;
}

static int test_overwrite_current(void)
{
	uint32_t offset, size;

	/* Test that we cannot overwrite current image */
	if (system_get_image_copy() == SYSTEM_IMAGE_RO) {
		offset = CONFIG_FW_RO_OFF;
		size = CONFIG_FW_RO_SIZE;
	} else {
		offset = CONFIG_FW_RW_OFF;
		size = CONFIG_FW_RW_SIZE;
	}

#ifdef EMU_BUILD
	mock_is_running_img = 1;
#endif

	VERIFY_NO_ERASE(offset, sizeof(testdata));
	VERIFY_NO_ERASE(offset + size - sizeof(testdata), sizeof(testdata));
	VERIFY_NO_WRITE(offset, sizeof(testdata), testdata);
	VERIFY_NO_WRITE(offset + size - sizeof(testdata), sizeof(testdata),
			testdata);

	return EC_SUCCESS;
}

static int test_overwrite_other(void)
{
	uint32_t offset, size;

	/* Test that we can overwrite the other image */
	if (system_get_image_copy() == SYSTEM_IMAGE_RW) {
		offset = CONFIG_FW_RO_OFF;
		size = CONFIG_FW_RO_SIZE;
	} else {
		offset = CONFIG_FW_RW_OFF;
		size = CONFIG_FW_RW_SIZE;
	}

#ifdef EMU_BUILD
	mock_is_running_img = 0;
#endif

	VERIFY_ERASE(offset, sizeof(testdata));
	VERIFY_ERASE(offset + size - sizeof(testdata), sizeof(testdata));
	VERIFY_WRITE(offset, sizeof(testdata), testdata);
	VERIFY_WRITE(offset + size - sizeof(testdata), sizeof(testdata),
		     testdata);

	return EC_SUCCESS;
}

static int test_op_failure(void)
{
	mock_flash_op_fail = EC_ERROR_UNKNOWN;
	VERIFY_NO_WRITE(CONFIG_FW_RO_OFF, sizeof(testdata), testdata);
	VERIFY_NO_WRITE(CONFIG_FW_RW_OFF, sizeof(testdata), testdata);
	VERIFY_NO_ERASE(CONFIG_FW_RO_OFF, CONFIG_FLASH_ERASE_SIZE);
	VERIFY_NO_ERASE(CONFIG_FW_RW_OFF, CONFIG_FLASH_ERASE_SIZE);
	mock_flash_op_fail = EC_SUCCESS;

	return EC_SUCCESS;
}

static int test_flash_info(void)
{
	struct ec_response_flash_info resp;

	TEST_ASSERT(test_send_host_command(EC_CMD_FLASH_INFO, 0, NULL, 0,
		    &resp, sizeof(resp)) == EC_RES_SUCCESS);

	TEST_CHECK((resp.flash_size == CONFIG_FLASH_SIZE) &&
		   (resp.write_block_size == CONFIG_FLASH_WRITE_SIZE) &&
		   (resp.erase_block_size == CONFIG_FLASH_ERASE_SIZE) &&
		   (resp.protect_block_size == CONFIG_FLASH_BANK_SIZE));
}

static int test_region_info(void)
{
	VERIFY_REGION_INFO(EC_FLASH_REGION_RO,
			   CONFIG_FW_RO_OFF, CONFIG_FW_RO_SIZE);
	VERIFY_REGION_INFO(EC_FLASH_REGION_RW,
			   CONFIG_FW_RW_OFF, CONFIG_FW_RW_SIZE);
	VERIFY_REGION_INFO(EC_FLASH_REGION_WP_RO,
			   CONFIG_FW_WP_RO_OFF, CONFIG_FW_WP_RO_SIZE);

	return EC_SUCCESS;
}

static int test_write_protect(void)
{
	/* Test we can control write protect GPIO */
	mock_wp = 0;
	ASSERT_WP_NO_FLAGS(EC_FLASH_PROTECT_GPIO_ASSERTED);

	mock_wp = 1;
	ASSERT_WP_FLAGS(EC_FLASH_PROTECT_GPIO_ASSERTED);

	/* Test software WP can be disable if nothing is actually protected */
	SET_WP_FLAGS(EC_FLASH_PROTECT_RO_AT_BOOT, 1);
	SET_WP_FLAGS(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	ASSERT_WP_NO_FLAGS(EC_FLASH_PROTECT_RO_AT_BOOT);

	/* Actually protect flash and test software WP cannot be disabled */
	SET_WP_FLAGS(EC_FLASH_PROTECT_RO_AT_BOOT, 1);
	SET_WP_FLAGS(EC_FLASH_PROTECT_ALL_NOW, 1);
	SET_WP_FLAGS(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	SET_WP_FLAGS(EC_FLASH_PROTECT_ALL_NOW, 0);
	ASSERT_WP_FLAGS(EC_FLASH_PROTECT_ALL_NOW | EC_FLASH_PROTECT_RO_AT_BOOT);

	/* Check we cannot erase anything */
	TEST_ASSERT(flash_physical_erase(CONFIG_FW_RO_OFF,
			CONFIG_FLASH_ERASE_SIZE) != EC_SUCCESS);
	TEST_ASSERT(flash_physical_erase(CONFIG_FW_RW_OFF,
			CONFIG_FLASH_ERASE_SIZE) != EC_SUCCESS);

	/* We should not even try to write/erase */
	VERIFY_NO_ERASE(CONFIG_FW_RO_OFF, CONFIG_FLASH_ERASE_SIZE);
	VERIFY_NO_ERASE(CONFIG_FW_RW_OFF, CONFIG_FLASH_ERASE_SIZE);
	VERIFY_NO_WRITE(CONFIG_FW_RO_OFF, sizeof(testdata), testdata);
	VERIFY_NO_WRITE(CONFIG_FW_RW_OFF, sizeof(testdata), testdata);

	return EC_SUCCESS;
}

static int test_boot_write_protect(void)
{
	/* Check write protect state persists through reboot */
	ASSERT_WP_FLAGS(EC_FLASH_PROTECT_RO_NOW | EC_FLASH_PROTECT_RO_AT_BOOT);
	TEST_ASSERT(flash_physical_erase(CONFIG_FW_RO_OFF,
			CONFIG_FLASH_ERASE_SIZE) != EC_SUCCESS);

	return EC_SUCCESS;
}

static int test_boot_no_write_protect(void)
{
	/* Check write protect is not enabled if WP GPIO is deasserted */
	ASSERT_WP_NO_FLAGS(EC_FLASH_PROTECT_RO_NOW);
	ASSERT_WP_FLAGS(EC_FLASH_PROTECT_RO_AT_BOOT);

	return EC_SUCCESS;
}

static int clean_up(void)
{
	system_set_scratchpad(0);
	SET_WP_FLAGS(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	return EC_SUCCESS;
}

static void reboot_to_clean_up(uint32_t flags)
{
	ccprintf("Rebooting to clear WP...\n");
	cflush();
	system_set_scratchpad(TEST_STATE_CLEAN_UP | flags);
	system_reset(SYSTEM_RESET_HARD);
}

static void reboot_to_next_step(uint32_t step)
{
	ccprintf("Rebooting to next test step...\n");
	cflush();
	system_set_scratchpad(step);
	system_reset(SYSTEM_RESET_HARD);
}

static void run_test_step1(void)
{
	test_reset();
	mock_wp = 0;

	RUN_TEST(test_read);
	RUN_TEST(test_overwrite_current);
	RUN_TEST(test_overwrite_other);
	RUN_TEST(test_op_failure);
	RUN_TEST(test_flash_info);
	RUN_TEST(test_region_info);
	RUN_TEST(test_write_protect);

	if (test_get_error_count())
		reboot_to_clean_up(CLEAN_UP_FLAG_FAILED);
	else
		reboot_to_next_step(TEST_STATE_STEP_2 | TEST_STATE_BOOT_WP_ON);
}

static void run_test_step2(void)
{
	RUN_TEST(test_boot_write_protect);

	if (test_get_error_count())
		reboot_to_clean_up(CLEAN_UP_FLAG_FAILED);
	else
		reboot_to_next_step(TEST_STATE_STEP_3);
}

static void run_test_step3(void)
{
	RUN_TEST(test_boot_no_write_protect);

	if (test_get_error_count())
		reboot_to_clean_up(CLEAN_UP_FLAG_FAILED);
	else
		reboot_to_clean_up(CLEAN_UP_FLAG_PASSED);
}

int TaskTest(void *data)
{
	uint32_t state = system_get_scratchpad();

	if (state & TEST_STATE_PASSED)
		ccprintf("Pass!\n");
	else if (state & TEST_STATE_FAILED)
		ccprintf("Fail!\n");

	if (state & TEST_STATE_STEP_2)
		run_test_step2();
	else if (state & TEST_STATE_STEP_3)
		run_test_step3();
	else if (state & TEST_STATE_CLEAN_UP)
		clean_up();
#ifdef EMU_BUILD
	else
		run_test_step1();
#endif

	return EC_SUCCESS;
}

#ifndef EMU_BUILD
void run_test(void)
{
	run_test_step1();
}
#endif
