/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_state_without_driver_info.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <stdint.h>
#include <string.h>

test_static const uint8_t default_fake_tpm_seed[] = {
	0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60, 0xf8, 0x5a, 0xa0,
	0xa6, 0x2c, 0xb3, 0xf5, 0xe2, 0xeb, 0xb9, 0xd8, 0x2f, 0xb5, 0x78,
	0x5c, 0x79, 0x82, 0xce, 0x06, 0x3f, 0xcc, 0x23, 0xb9, 0xe7,
};

test_static const uint8_t zero_fake_tpm_seed[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#ifdef SECTION_IS_RO
test_static uint8_t tpm_seed[FP_CONTEXT_TPM_BYTES];
#endif

test_static int test_tpm_seed_before_reboot(void)
{
	TEST_ASSERT_ARRAY_EQ(global_context.tpm_seed, zero_fake_tpm_seed,
			     FP_CONTEXT_TPM_BYTES);
	memcpy(global_context.tpm_seed, default_fake_tpm_seed,
	       FP_CONTEXT_TPM_BYTES);
	TEST_ASSERT_ARRAY_EQ(global_context.tpm_seed, default_fake_tpm_seed,
			     FP_CONTEXT_TPM_BYTES);
	return EC_SUCCESS;
}

test_static int test_tpm_seed_after_reboot(void)
{
	TEST_ASSERT_ARRAY_EQ(global_context.tpm_seed, zero_fake_tpm_seed,
			     FP_CONTEXT_TPM_BYTES);
	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	ccprints("Step 1: tpm_seed_clear");
	cflush();

	RUN_TEST(test_tpm_seed_before_reboot);

	if (test_get_error_count()) {
		test_reboot_to_next_step(TEST_STATE_FAILED);
	} else {
		test_reboot_to_next_step(TEST_STATE_STEP_2);
	}
}

test_static void run_test_step2(void)
{
	ccprints("Step 2: tpm_seed_clear");
	cflush();

	RUN_TEST(test_tpm_seed_after_reboot);

	if (test_get_error_count()) {
		test_reboot_to_next_step(TEST_STATE_FAILED);
	} else {
		test_reboot_to_next_step(TEST_STATE_PASSED);
	}
}

void test_run_step(uint32_t state)
{
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1)) {
		run_test_step1();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		run_test_step2();
	}
}
extern "C" int task_test(void *unused)
{
	if (IS_ENABLED(SECTION_IS_RW))
		test_run_multistep();
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	crec_msleep(100); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
