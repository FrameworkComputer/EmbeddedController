/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "multistep_test.h"
#include "panic.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(panic_hw_test, LOG_LEVEL_INF);

struct reg_vals {
	int index;
	uint32_t val;
};

/* TODO(b/342504464): add a version for PANIC_STRIP_GPR=y. */
static const struct reg_vals expected_regs[] = {
	{ .index = CORTEX_PANIC_REGISTER_R4, .val = 0xecec0004 },
	{ .index = CORTEX_PANIC_REGISTER_R5, .val = 0xecec0005 },
	{ .index = CORTEX_PANIC_REGISTER_R6, .val = 0xecec0006 },
	{ .index = CORTEX_PANIC_REGISTER_R7, .val = 0xecec0007 },
	{ .index = CORTEX_PANIC_REGISTER_R8, .val = 0xecec0008 },
	{ .index = CORTEX_PANIC_REGISTER_R9, .val = 0xecec0009 },
	{ .index = CORTEX_PANIC_REGISTER_R10, .val = 0xecec000a },
	{ .index = CORTEX_PANIC_REGISTER_R11, .val = 0xecec000b },
};

static void test_panic(void)
{
	LOG_INF("Step 1: Panic");

	if (IS_ENABLED(CONFIG_CPU_CORTEX_M0)) {
		__asm__ volatile("ldr r1, =0xecec0001\n"
				 "ldr r2, =0xecec0002\n"
				 "ldr r3, =0xecec0003\n"
				 "ldr r4, =0xecec0004\n"
				 "ldr r5, =0xecec0005\n"
				 "ldr r6, =0xecec0006\n"
				 "ldr r7, =0xecec0007\n"
				 "ldr r0, =0xecec0008\n"
				 "mov r8, r0\n"
				 "ldr r0, =0xecec0009\n"
				 "mov r9, r0\n"
				 "ldr r0, =0xecec000a\n"
				 "mov r10, r0\n"
				 "ldr r0, =0xecec000b\n"
				 "mov r11, r0\n"
				 "ldr r0, =0xecec000e\n"
				 "mov r14, r0\n"
				 /* Undefined instruction. */
				 "udf #0\n");
	} else if (CONFIG_CPU_CORTEX_M) {
		__asm__ volatile("ldr r0, =0xecec0000\n"
				 "ldr r1, =0xecec0001\n"
				 "ldr r2, =0xecec0002\n"
				 "ldr r3, =0xecec0003\n"
				 "ldr r4, =0xecec0004\n"
				 "ldr r5, =0xecec0005\n"
				 "ldr r6, =0xecec0006\n"
				 "ldr r7, =0xecec0007\n"
				 "ldr r8, =0xecec0008\n"
				 "ldr r9, =0xecec0009\n"
				 "ldr r10, =0xecec000a\n"
				 "ldr r11, =0xecec000b\n"
				 "ldr r14, =0xecec000e\n"
				 /* Undefined instruction. */
				 "udf #0\n");
	}
	/* Should never reach this. */
	zassert_unreachable();
}

static void test_panic_data(void)
{
	if (IS_ENABLED(CONFIG_ARM)) {
		struct panic_data *const pdata = panic_get_data();
		int i;

		LOG_INF("Step 2: Read panic data");
		for (i = 0; i < ARRAY_SIZE(expected_regs); i++) {
			zassert_equal(expected_regs[i].val,
				      pdata->cm.regs[expected_regs[i].index]);
		}
	}
}

static void (*test_steps[])(void) = { test_panic, test_panic_data };

MULTISTEP_TEST(panic, test_steps)
