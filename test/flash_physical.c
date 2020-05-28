/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chip/stm32/flash-f.h"
#include "flash.h"
#include "panic.h"
#include "test_util.h"

struct flash_info {
	int num_flash_banks;
	int write_protect_bank_offset;
	int write_protect_bank_count;
};

#if defined(CHIP_VARIANT_STM32F412)
struct flash_info flash_info = {
	.num_flash_banks = 12,
	.write_protect_bank_offset = 0,
	.write_protect_bank_count = 5,
};
#elif defined(CHIP_VARIANT_STM32H7X3)
struct flash_info flash_info = {
	.num_flash_banks = 16,
	.write_protect_bank_offset = 0,
	.write_protect_bank_count = 6,
};
#else
#error "Flash info not defined for this chip. Please add it."
#endif


test_static int test_lock_option_bytes(void)
{
	TEST_EQ(flash_option_bytes_locked(), true, "%d");

	unlock_flash_option_bytes();

	TEST_EQ(flash_option_bytes_locked(), false, "%d");

	lock_flash_option_bytes();

	TEST_EQ(flash_option_bytes_locked(), true, "%d");

	unlock_flash_option_bytes();

	TEST_EQ(flash_option_bytes_locked(), false, "%d");

	return EC_SUCCESS;
}

test_static int test_disable_option_bytes(void)
{
	TEST_EQ(flash_option_bytes_locked(), false, "%d");

	disable_flash_option_bytes();

	TEST_EQ(flash_option_bytes_locked(), true, "%d");

	/* Since we've disabled the option bytes we'll get a bus fault. */
	ignore_bus_fault(1);

	unlock_flash_option_bytes();

	ignore_bus_fault(0);

	/* Option bytes should still be locked. */
	TEST_EQ(flash_option_bytes_locked(), true, "%d");

	return EC_SUCCESS;
}

test_static int test_lock_flash_control_register(void)
{
	TEST_EQ(flash_control_register_locked(), true, "%d");

	unlock_flash_control_register();

	TEST_EQ(flash_control_register_locked(), false, "%d");

	lock_flash_control_register();

	TEST_EQ(flash_control_register_locked(), true, "%d");

	unlock_flash_control_register();

	TEST_EQ(flash_control_register_locked(), false, "%d");

	return EC_SUCCESS;
}

test_static int test_disable_flash_control_register(void)
{
	TEST_EQ(flash_control_register_locked(), false, "%d");

	disable_flash_control_register();

	TEST_EQ(flash_control_register_locked(), true, "%d");

	/* Since we've disabled the option bytes we'll get a bus fault. */
	ignore_bus_fault(1);

	unlock_flash_control_register();

	ignore_bus_fault(0);

	/* Control register should still be locked. */
	TEST_EQ(flash_control_register_locked(), true, "%d");

	return EC_SUCCESS;
}

test_static int test_flash_config(void)
{
	TEST_EQ(PHYSICAL_BANKS, flash_info.num_flash_banks, "%d");
	TEST_EQ(WP_BANK_OFFSET, flash_info.write_protect_bank_offset, "%d");
	TEST_EQ(WP_BANK_COUNT, flash_info.write_protect_bank_count, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	ccprintf("Running flash physical test\n");
	RUN_TEST(test_flash_config);
	/*
	 * TODO(b/157692395): These should be implemented for the STM32H743 as
	 * well.
	 */
#if defined(CHIP_VARIANT_STM32F412)
	RUN_TEST(test_lock_option_bytes);
	RUN_TEST(test_disable_option_bytes);
	RUN_TEST(test_lock_flash_control_register);
	RUN_TEST(test_disable_flash_control_register);
#endif
	test_print_result();
}
