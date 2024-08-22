/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "multistep_test.h"
#include "panic.h"
#include "system.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

LOG_MODULE_REGISTER(panic_data, LOG_LEVEL_INF);

static void crash_system(void)
{
	__ASSERT_NO_MSG(0);
}

static void check_panic_data(void)
{
	struct panic_data *pdata = panic_get_data();
#ifdef CONFIG_ARM
	uint32_t crash_addr = (uint32_t)crash_system;
	/* Estimated end of the crash_system function. */
	uint32_t crash_end = (uint32_t)crash_system + 0x20;
	uint32_t lr = pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_LR];

	/* Make sure Link Register is stored correctly and points at the
	 * crash_system function. */
	zassert_true((crash_addr <= lr) && (crash_end >= lr));

	/* Check panic flags. */
	zassert_equal(pdata->flags, PANIC_DATA_FLAG_FRAME_VALID |
					    PANIC_DATA_FLAG_OLD_HOSTEVENT);
#endif
}

static void test_crash(void)
{
	LOG_INF("Crash the system!");
	cflush();
	crash_system();
	/* Should never reach this. */
	zassert_unreachable();
}

static void test_soft_reboot(void)
{
	check_panic_data();
	LOG_INF("Perform soft reboot");
	cflush();
	system_reset(0);
	/* Should never reach this. */
	zassert_unreachable();
}

static void test_hard_reboot(void)
{
	check_panic_data();
	LOG_INF("Perform soft reboot");
	cflush();
	system_reset(SYSTEM_RESET_HARD);
	/* Should never reach this. */
	zassert_unreachable();
}

static void test_check_panic(void)
{
	LOG_INF("Check panic data");
	check_panic_data();
}

static void (*test_steps[])(void) = { test_crash, test_soft_reboot,
				      test_hard_reboot, test_check_panic };

MULTISTEP_TEST(panic_data, test_steps)
