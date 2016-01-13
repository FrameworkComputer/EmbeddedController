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
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int mock_wp = -1;

static int mock_flash_op_fail = EC_SUCCESS;

const char *testdata = "TestData00000000"; /* 16 bytes excluding NULL end */

char flash_recorded_data[128];

#define BOOT_WP_MASK TEST_STATE_MASK(TEST_STATE_STEP_2)

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

int flash_pre_op(void)
{
	return mock_flash_op_fail;
}

int gpio_get_level(enum gpio_signal signal)
{
	if (mock_wp == -1)
		mock_wp = !!(test_get_state() & BOOT_WP_MASK);

#if defined(CONFIG_WP_ACTIVE_HIGH)
	if (signal == GPIO_WP)
		return mock_wp;
#else
	if (signal == GPIO_WP_L)
		return !mock_wp;
#endif

	/* Signal other than write protect. Just return 0. */
	return 0;
}

/*****************************************************************************/
/* Test utilities */

static void record_flash(int offset, int size)
{
	memcpy(flash_recorded_data, __host_flash + offset, size);
}

static int verify_flash(int offset, int size)
{
	TEST_ASSERT_ARRAY_EQ(flash_recorded_data, __host_flash + offset, size);
	return EC_SUCCESS;
}

static int verify_write(int offset, int size, const char *data)
{
	int i;

	for (i = 0; i < size; ++i)
		if (__host_flash[offset + i] != data[i])
			return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int verify_erase(int offset, int size)
{
	int i;

	for (i = 0; i < size; ++i)
		if ((__host_flash[offset + i] & 0xff) != 0xff)
			return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}


#define VERIFY_NO_WRITE(off, sz, d) \
	do { \
		record_flash(off, sz); \
		TEST_ASSERT(host_command_write(off, sz, d) != EC_SUCCESS); \
		TEST_ASSERT(verify_flash(off, sz) == EC_SUCCESS); \
	} while (0)

#define VERIFY_NO_ERASE(off, sz) \
	do { \
		record_flash(off, sz); \
		TEST_ASSERT(host_command_erase(off, sz) != EC_SUCCESS); \
		TEST_ASSERT(verify_flash(off, sz) == EC_SUCCESS); \
	} while (0)

#define VERIFY_WRITE(off, sz, d) \
	do { \
		TEST_ASSERT(host_command_write(off, sz, d) == EC_SUCCESS); \
		TEST_ASSERT(verify_write(off, sz, d) == EC_SUCCESS); \
	} while (0)

#define VERIFY_ERASE(off, sz) \
	do { \
		TEST_ASSERT(host_command_erase(off, sz) == EC_SUCCESS); \
		TEST_ASSERT(verify_erase(off, sz) == EC_SUCCESS); \
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
	TEST_ASSERT_ARRAY_EQ(buf, (char *)CONFIG_PROGRAM_MEMORY_BASE,
			     sizeof(buf));

	return EC_SUCCESS;
}

static int test_is_erased(void)
{
	int i;

#ifdef EMU_BUILD
	memset(__host_flash, 0xff, 1024);
	TEST_ASSERT(flash_is_erased(0, 1024));

	for (i = 0; i < 1024; ++i) {
		__host_flash[i] = 0xec;
		TEST_ASSERT(!flash_is_erased(0, 1024));
		__host_flash[i] = 0xff;
	}
#else
	ccprintf("Skip. Emulator only test.\n");
#endif

	return EC_SUCCESS;
}

static int test_overwrite_current(void)
{
	uint32_t offset, size;

	/* Test that we cannot overwrite current image */
	if (system_get_image_copy() == SYSTEM_IMAGE_RO) {
		offset = CONFIG_RO_STORAGE_OFF;
		size = CONFIG_RO_SIZE;
	} else {
		offset = CONFIG_RW_STORAGE_OFF;
		size = CONFIG_RW_SIZE;
	}

#ifdef EMU_BUILD
	mock_is_running_img = 1;
#endif

	VERIFY_NO_ERASE(offset, strlen(testdata));
	VERIFY_NO_ERASE(offset + size - strlen(testdata), strlen(testdata));
	VERIFY_NO_WRITE(offset, strlen(testdata), testdata);
	VERIFY_NO_WRITE(offset + size - strlen(testdata), strlen(testdata),
			testdata);

	return EC_SUCCESS;
}

static int test_overwrite_other(void)
{
	uint32_t offset, size;

	/* Test that we can overwrite the other image */
	if (system_get_image_copy() == SYSTEM_IMAGE_RW) {
		offset = CONFIG_RO_STORAGE_OFF;
		size = CONFIG_RO_SIZE;
	} else {
		offset = CONFIG_RW_STORAGE_OFF;
		size = CONFIG_RW_SIZE;
	}

#ifdef EMU_BUILD
	mock_is_running_img = 0;
#endif

	VERIFY_ERASE(offset, strlen(testdata));
	VERIFY_ERASE(offset + size - strlen(testdata), strlen(testdata));
	VERIFY_WRITE(offset, strlen(testdata), testdata);
	VERIFY_WRITE(offset + size - strlen(testdata), strlen(testdata),
		     testdata);

	return EC_SUCCESS;
}

static int test_op_failure(void)
{
	mock_flash_op_fail = EC_ERROR_UNKNOWN;
	VERIFY_NO_WRITE(CONFIG_RO_STORAGE_OFF, sizeof(testdata), testdata);
	VERIFY_NO_WRITE(CONFIG_RW_STORAGE_OFF, sizeof(testdata), testdata);
	VERIFY_NO_ERASE(CONFIG_RO_STORAGE_OFF, CONFIG_FLASH_ERASE_SIZE);
	VERIFY_NO_ERASE(CONFIG_RW_STORAGE_OFF, CONFIG_FLASH_ERASE_SIZE);
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
			   CONFIG_EC_PROTECTED_STORAGE_OFF +
			   CONFIG_RO_STORAGE_OFF, CONFIG_RO_SIZE);
	VERIFY_REGION_INFO(EC_FLASH_REGION_RW,
			   CONFIG_EC_WRITABLE_STORAGE_OFF +
			   CONFIG_RW_STORAGE_OFF, CONFIG_RW_SIZE);
	VERIFY_REGION_INFO(EC_FLASH_REGION_WP_RO,
			   CONFIG_WP_STORAGE_OFF, CONFIG_WP_STORAGE_SIZE);

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
	TEST_ASSERT(flash_physical_erase(CONFIG_RO_STORAGE_OFF,
			CONFIG_FLASH_ERASE_SIZE) != EC_SUCCESS);
	TEST_ASSERT(flash_physical_erase(CONFIG_RW_STORAGE_OFF,
			CONFIG_FLASH_ERASE_SIZE) != EC_SUCCESS);

	/* We should not even try to write/erase */
	VERIFY_NO_ERASE(CONFIG_RO_STORAGE_OFF, CONFIG_FLASH_ERASE_SIZE);
	VERIFY_NO_ERASE(CONFIG_RW_STORAGE_OFF, CONFIG_FLASH_ERASE_SIZE);
	VERIFY_NO_WRITE(CONFIG_RO_STORAGE_OFF, sizeof(testdata), testdata);
	VERIFY_NO_WRITE(CONFIG_RW_STORAGE_OFF, sizeof(testdata), testdata);

	return EC_SUCCESS;
}

static int test_boot_write_protect(void)
{
	/* Check write protect state persists through reboot */
	ASSERT_WP_FLAGS(EC_FLASH_PROTECT_RO_NOW | EC_FLASH_PROTECT_RO_AT_BOOT);
	TEST_ASSERT(flash_physical_erase(CONFIG_RO_STORAGE_OFF,
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

int test_clean_up_(void)
{
	SET_WP_FLAGS(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	return EC_SUCCESS;
}

void test_clean_up(void)
{
	test_clean_up_(); /* Throw away return value */
}

static void run_test_step1(void)
{
	test_reset();
	mock_wp = 0;

	RUN_TEST(test_read);
	RUN_TEST(test_is_erased);
	RUN_TEST(test_overwrite_current);
	RUN_TEST(test_overwrite_other);
	RUN_TEST(test_op_failure);
	RUN_TEST(test_flash_info);
	RUN_TEST(test_region_info);
	RUN_TEST(test_write_protect);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_2);
}

static void run_test_step2(void)
{
	RUN_TEST(test_boot_write_protect);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_3);
}

static void run_test_step3(void)
{
	RUN_TEST(test_boot_no_write_protect);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_PASSED);
}

void test_run_step(uint32_t state)
{
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1))
		run_test_step1();
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2))
		run_test_step2();
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_3))
		run_test_step3();
}

int task_test(void *data)
{
	test_run_multistep();
	return EC_SUCCESS;
}

void run_test(void)
{
	msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
