/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(null_pointer, LOG_LEVEL_INF);

ZTEST_SUITE(null_pointer, NULL, NULL, NULL, NULL, NULL);

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_equal(reason, K_ERR_CPU_EXCEPTION);
	ztest_set_fault_valid(false);
}

void null_pointer_dereference(void)
{
	volatile uint32_t *null_ptr = NULL;

	ztest_set_fault_valid(true);
	LOG_INF("The value of null_ptr after dereferencing is: %d", *null_ptr);

	zassert_unreachable();
}

ZTEST(null_pointer, test_null_pointer_dereference)
{
	null_pointer_dereference();

	zassert_unreachable();
}
