/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(abort_hw_test, LOG_LEVEL_INF);

static void *expected_fault_addr;

ZTEST_SUITE(abort_test, NULL, NULL, NULL, NULL, NULL);

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_equal(reason, K_ERR_KERNEL_PANIC);
	zassert_not_null(expected_fault_addr);

	ztest_set_fault_valid(false);

	/* Estimated end of the abort function, which is short. */
	uint32_t fn_end = (uint32_t)expected_fault_addr + 0x40;
#if defined(CONFIG_ARM)
	uint32_t pc = pEsf->basic.pc;
#elif defined(CONFIG_RISCV)
	uint32_t pc = pEsf->mepc;
#else
	uint32_t pc = 0;
	zassert_unreachable("Test not supported on this architecture");
#endif

	/* Make sure Program Counter is stored correctly and points at the abort
	 * function.
	 */
	zassert_true(pc >= ((uint32_t)expected_fault_addr) && (pc <= fn_end),
		     "PC 0x%x not in range [0x%x, 0x%x]", pc,
		     (uint32_t)expected_fault_addr, fn_end);

	expected_fault_addr = NULL;
}

ZTEST(abort_test, test_abort)
{
	expected_fault_addr = (void *)abort;
	ztest_set_fault_valid(true);

	LOG_INF("Calling abort\n");
	cflush();
	abort();

	/* Should never reach this. */
	zassert_unreachable();
}
