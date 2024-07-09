/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "multistep_test.h"
#include "panic.h"
#include "system.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(abort_hw_test, LOG_LEVEL_INF);

static void test_abort(void)
{
	LOG_INF("Calling abort\n");
	cflush();
	abort();
	/* Should never reach this. */
	zassert_unreachable();
}

static void test_panic_data(void)
{
#ifdef CONFIG_ARM
	struct panic_data *const pdata = panic_get_data();
	uint32_t abort_addr = (uint32_t)abort;
	/* Estimated end of the abort function, which is short. */
	uint32_t abort_end = (uint32_t)abort + 0x40;
	uint32_t pc = pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PC];

	/* Make sure Program Counter is stored correctly and points at the abort
	 * function.
	 */
	zassert_true((abort_addr <= pc) && (abort_end >= pc));
#endif
}

static void (*test_steps[])(void) = { test_abort, test_panic_data };

MULTISTEP_TEST(abort, test_steps)
