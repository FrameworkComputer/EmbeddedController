/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef __cplusplus
extern "C" {
#endif
#include <zephyr/linker/linker-defs.h>
#ifdef __cplusplus
}
#endif

#include "common.h"
#include "ec_commands.h"
#include "multistep_test.h"
#include "system.h"
#include "task.h"

#include <zephyr/logging/log.h>

#include <algorithm>

LOG_MODULE_REGISTER(tpm_seed_clear_hw_test, LOG_LEVEL_INF);

#include "fpsensor/fpsensor_state.h"

#include <stdint.h>
#include <string.h>

#ifdef CONFIG_CROS_EC_RW
static const uint8_t default_fake_tpm_seed[] = {
	0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60, 0xf8, 0x5a, 0xa0,
	0xa6, 0x2c, 0xb3, 0xf5, 0xe2, 0xeb, 0xb9, 0xd8, 0x2f, 0xb5, 0x78,
	0x5c, 0x79, 0x82, 0xce, 0x06, 0x3f, 0xcc, 0x23, 0xb9, 0xe7,
};

static const uint8_t zero_fake_tpm_seed[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

void test_tpm_seed_before_reboot(void)
{
	LOG_INF("Step 1: tpm_seed_clear");
	cflush();

	zassert_mem_equal(global_context.tpm_seed.data(), zero_fake_tpm_seed,
			  FP_CONTEXT_TPM_BYTES);
	std::ranges::copy(default_fake_tpm_seed,
			  global_context.tpm_seed.begin());
	zassert_mem_equal(global_context.tpm_seed.data(), default_fake_tpm_seed,
			  FP_CONTEXT_TPM_BYTES);
	system_reset(SYSTEM_RESET_HARD);
}

void test_tpm_seed_after_reboot(void)
{
	LOG_INF("Step 2: tpm_seed_clear");
	cflush();

	zassert_mem_equal(global_context.tpm_seed.data(), zero_fake_tpm_seed,
			  FP_CONTEXT_TPM_BYTES);
}

static void (*test_steps[])(void) = { test_tpm_seed_before_reboot,
				      test_tpm_seed_after_reboot };

MULTISTEP_TEST(tpm_seed_clear, test_steps)
#endif /* CONFIG_CROS_EC_RW */
