/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "multistep_test.h"
#include "system.h"
#include "write_protect.h"

#ifdef CONFIG_EEPROM_CBI_WP
#warning "EEPROM CBI WP tests not implemented."
#endif

static int check_image_and_hardware_write_protect(void)
{
	bool wp;

	if (system_get_image_copy() != EC_IMAGE_RO) {
		ccprintf("This test is only works when running RO\n");
		return -ENOTSUP;
	}

	wp = write_protect_is_asserted();

	if (!wp) {
		ccprintf("Hardware write protect (GPIO_WP) must be enabled\n");
		return -ENOTSUP;
	}

	return 0;
}

static void test_wp_enable(void)
{
	int rv;

	zassert_equal(check_image_and_hardware_write_protect(), 0);

	/* Equivalent of ectool --name=cros_fp flashprotect enable */
	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
				    EC_FLASH_PROTECT_RO_AT_BOOT);

	zassert_equal(rv, EC_SUCCESS);
	cflush();
	system_reset(SYSTEM_RESET_HARD);
}

static void test_wp_disable(void)
{
	int rv;

	zassert_equal(check_image_and_hardware_write_protect(), 0);

	/* Equivalent of ectool --name=cros_fp flashprotect disable */
	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT, 0);

	zassert_not_equal(rv, EC_SUCCESS);
}

static void (*test_steps[])(void) = { test_wp_enable, test_wp_disable };

MULTISTEP_TEST(flash_write_protect, test_steps)
