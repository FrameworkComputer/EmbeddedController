/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "gpio.h"
#include "string.h"
#include "system.h"
#include "test_util.h"

test_static int check_image_and_hardware_write_protect(void)
{
	if (system_get_image_copy() != EC_IMAGE_RO) {
		ccprintf("This test is only works when running RO\n");
		return EC_ERROR_UNKNOWN;
	}

	if (gpio_get_level(GPIO_WP) != 1) {
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
	rv = flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
			       EC_FLASH_PROTECT_RO_AT_BOOT);

	return rv;
}

test_static int test_flash_write_protect_disable(void)
{
	int rv;

	TEST_EQ(check_image_and_hardware_write_protect(), EC_SUCCESS, "%d");

	/* Equivalent of ectool --name=cros_fp flashprotect disable */
	rv = flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	TEST_NE(rv, EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	ccprintf("Step 1: Flash write protect test\n");
	RUN_TEST(test_flash_write_protect_enable);
}

test_static void run_test_step2(void)
{
	ccprintf("Step 2: Flash write protect test\n");
	RUN_TEST(test_flash_write_protect_disable);
}

void run_test(int argc, char **argv)
{
	if (argc < 2) {
		ccprintf("usage: runtest <test_step_number>\n");
		return;
	}

	/*
	 * TODO(157059753): replace with test_run_multistep when scratchpad
	 * works.
	 */
	if (strncmp(argv[1], "1", 1) == 0)
		run_test_step1();
	else if (strncmp(argv[1], "2", 1) == 0)
		run_test_step2();
}
