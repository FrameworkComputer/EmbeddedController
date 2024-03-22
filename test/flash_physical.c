/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chip/stm32/flash-regs.h"
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
#elif defined(CHIP_VARIANT_NPCX9MFP)
struct flash_info flash_info = {
	.num_flash_banks = 16,
	.write_protect_bank_offset = 0,
	/* Helipilot's CONFIG_RO_SIZE is 128 KB and write protect size is 64 KB,
	 * so there should be 2 banks
	 */
	.write_protect_bank_count = 2,
};
#else
#error "Flash info not defined for this chip. Please add it."
#endif

/* The Option Bytes are stm32 specific and there is no real analog in
 * NPCX chip.
 */
#ifndef CHIP_NPCX
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
#endif

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
	TEST_EQ(crec_flash_total_banks(), flash_info.num_flash_banks, "%d");
	TEST_EQ(WP_BANK_OFFSET, flash_info.write_protect_bank_offset, "%d");
	TEST_EQ(WP_BANK_COUNT, flash_info.write_protect_bank_count, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	ccprintf("Running flash physical test\n");
	RUN_TEST(test_flash_config);

/* The Helipilot NPCX board does not support option bytes from STM32 chips */
#ifndef BASEBOARD_HELIPILOT
	RUN_TEST(test_lock_option_bytes);
	RUN_TEST(test_disable_option_bytes);
#endif

	RUN_TEST(test_lock_flash_control_register);
	RUN_TEST(test_disable_flash_control_register);
	test_print_result();
}
