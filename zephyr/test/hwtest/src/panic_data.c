/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "multistep_test.h"
#include "panic.h"
#include "system.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

LOG_MODULE_REGISTER(panic_data, LOG_LEVEL_INF);

static void test_crash(void);
static void crash_system(void);

#ifdef CONFIG_LTO
static const uint32_t crash_addr = (uint32_t)test_crash;
#else
static const uint32_t crash_addr = (uint32_t)crash_system;
#endif

/* Estimated end of the crash function. */
static const uint32_t crash_end = crash_addr + 0x20;

static void crash_system(void)
{
	/* TODO(b/423904871): We should be able to use __ASSERT_NO_MSG when LTO
	 * is enabled; we should prevent __ASSERT_NO_MSG from being outlined. */
	if (IS_ENABLED(CONFIG_LTO)) {
		__ASSERT_UNREACHABLE;
	} else {
		__ASSERT_NO_MSG(0);
	}
}

static void check_panic_data(void)
{
	struct panic_data *pdata = panic_get_data();
#ifdef CONFIG_ARM
	uint32_t lr = pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_LR];

	/* Make sure Link Register is stored correctly and points at the
	 * crash_system function. */
	zassert_true((crash_addr <= lr) && (crash_end >= lr));

	/* Check panic flags. */
	zassert_equal(pdata->flags, PANIC_DATA_FLAG_FRAME_VALID |
					    PANIC_DATA_FLAG_OLD_HOSTEVENT |
					    (IS_ENABLED(SECTION_IS_RW) ?
						     PANIC_DATA_FLAG_RW_IMAGE :
						     PANIC_DATA_FLAG_RO_IMAGE));
#endif
}

static void test_crash(void)
{
	LOG_INF("Crash the system!");
	cflush();
	crash_system();
	zassert_unreachable();
}

static void test_soft_reboot(void)
{
	check_panic_data();
	LOG_INF("Perform soft reboot");
	cflush();
	system_reset(0);
	zassert_unreachable();
}

static void test_hard_reboot(void)
{
	check_panic_data();
	LOG_INF("Perform hard reboot");
	cflush();
	system_reset(SYSTEM_RESET_HARD);
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
