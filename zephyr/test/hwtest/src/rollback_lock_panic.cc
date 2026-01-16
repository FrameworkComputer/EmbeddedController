/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

#include <cstdint>

LOG_MODULE_REGISTER(rollback_lock_panic, LOG_LEVEL_INF);

ZTEST_SUITE(rollback_lock_panic, NULL, NULL, NULL, NULL, NULL);

#ifdef __cplusplus
extern "C" {
#endif

/* Mock the MPU lock function to fail */
int mpu_lock_rollback(bool lock)
{
	return -ENOENT;
}

/* Import internal functions to test */
uint32_t unlock_rollback(void);
void lock_rollback(uint32_t key);

#ifdef __cplusplus
}
#endif

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_equal(reason, K_ERR_KERNEL_PANIC);
	ztest_set_fault_valid(false);
}

ZTEST(rollback_lock_panic, test_unlock_crash)
{
	ztest_set_fault_valid(true);
	unlock_rollback();

	/* Should never reach this due to panic */
	zassert_unreachable();
}

ZTEST(rollback_lock_panic, test_lock_crash)
{
	ztest_set_fault_valid(true);
	lock_rollback(/*key=*/0);

	/* Should never reach this due to panic */
	zassert_unreachable();
}
