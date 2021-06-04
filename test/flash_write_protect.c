/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "gpio.h"
#include "string.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

test_static int check_image_and_hardware_write_protect(void)
{
	int wp;

	if (system_get_image_copy() != EC_IMAGE_RO) {
		ccprintf("This test is only works when running RO\n");
		return EC_ERROR_UNKNOWN;
	}

#ifdef CONFIG_WP_ALWAYS
        wp = 1;
#elif defined(CONFIG_WP_ACTIVE_HIGH)
        wp = gpio_get_level(GPIO_WP);
#else
	wp = !gpio_get_level(GPIO_WP_L);
#endif

	if (!wp) {
		ccprintf("Hardware write protect (GPIO_WP) must be enabled\n");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

test_static int test_flash_write_protect_enable(void)
{
	int rv;

	TEST_EQ(check_image_and_hardware_write_protect(), EC_SUCCESS, "%d");

	/* Equivalent of ectool --name=cros_fp flashprotect enable */
	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
				    EC_FLASH_PROTECT_RO_AT_BOOT);

	return rv;
}

test_static int test_flash_write_protect_disable(void)
{
	int rv;

	TEST_EQ(check_image_and_hardware_write_protect(), EC_SUCCESS, "%d");

	/* Equivalent of ectool --name=cros_fp flashprotect disable */
	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	TEST_NE(rv, EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	ccprintf("Step 1: Flash write protect test\n");
	RUN_TEST(test_flash_write_protect_enable);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_2);
}

test_static void run_test_step2(void)
{
	ccprintf("Step 2: Flash write protect test\n");
	RUN_TEST(test_flash_write_protect_disable);

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
}

int task_test(void *unused)
{
	test_run_multistep();
	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
