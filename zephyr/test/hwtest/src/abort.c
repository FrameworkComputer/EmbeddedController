/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <inttypes.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(abort_hw_test, LOG_LEVEL_INF);

static uintptr_t expected_fault_addr;

ZTEST_SUITE(abort_test, NULL, NULL, NULL, NULL, NULL);

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_equal(reason, K_ERR_KERNEL_PANIC);
	zassert_not_equal(expected_fault_addr, 0,
			  "Expected fault address not set");

	ztest_set_fault_valid(false);

	/* Estimated end of the abort function, which is short. */
	uintptr_t fn_end = expected_fault_addr + 0x40;
#if defined(CONFIG_ARM)
	uintptr_t pc = pEsf->basic.pc;
#elif defined(CONFIG_RISCV)
	uintptr_t pc = pEsf->mepc;
#else
	uintptr_t pc = 0;
	zassert_unreachable("Test not supported on this architecture");
#endif

	/* Make sure Program Counter is stored correctly and points at the abort
	 * function.
	 */
	zassert_true(pc >= expected_fault_addr && (pc <= fn_end),
		     "PC 0x%" PRIxPTR " not in range [0x%" PRIxPTR
		     ", 0x%" PRIxPTR "]",
		     pc, expected_fault_addr, fn_end);

	expected_fault_addr = 0;
}

ZTEST(abort_test, test_abort)
{
	expected_fault_addr = (uintptr_t)abort;
	ztest_set_fault_valid(true);

	LOG_INF("Calling abort\n");
	cflush();
	abort();

	zassert_unreachable();
}
