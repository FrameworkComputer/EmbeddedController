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

/*
 * This is the index of RA in the panic_data.riscv.regs array, not the
 * architectural register number. The layout is defined in
 * https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/shim/src/panic.c;l=79;drc=f66d5f641b3a9aa8a715fb29bd0ce6b2bb96ac8a
 */
static const uint32_t RISCV_PANIC_REG_RA = 29;

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
	uint32_t lr = 0;
	uint32_t expected_flags = PANIC_DATA_FLAG_OLD_HOSTEVENT;

	if (IS_ENABLED(SECTION_IS_RW)) {
		expected_flags |= PANIC_DATA_FLAG_RW_IMAGE;
	} else {
		expected_flags |= PANIC_DATA_FLAG_RO_IMAGE;
	}

	if (IS_ENABLED(CONFIG_ARM)) {
		lr = pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_LR];
		/*
		 * This flag is for ARM only.
		 * https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/shim/src/panic.c;l=152-154;drc=f66d5f641b3a9aa8a715fb29bd0ce6b2bb96ac8a
		 * https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/include/panic_defs.h;l=169-170;drc=ed1fa38550a14fd0aa35f1d7c79bf0aa3069acf6
		 */
		expected_flags |= PANIC_DATA_FLAG_FRAME_VALID;
	} else if (IS_ENABLED(CONFIG_RISCV)) {
		lr = pdata->riscv.regs[RISCV_PANIC_REG_RA];
	} else {
		zassert_true(false, "Unsupported architecture");
	}

	/* Make sure Link Register is stored correctly and points at the
	 * crash_system function. */
	zassert_true((crash_addr <= lr) && (crash_end >= lr));

	/* Check panic flags. */
	zassert_equal(pdata->flags, expected_flags);
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
