/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "assert.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#if !(defined(CORE_CORTEX_M) || defined(CORE_CORTEX_M0))
#error "Architecture not supported"
#endif

struct reg_vals {
	int index;
	uint32_t val;
};

static const struct reg_vals EXPECTED[] = {
	{ .index = CORTEX_PANIC_REGISTER_R4, .val = 0xecec0004 },
	{ .index = CORTEX_PANIC_REGISTER_R5, .val = 0xecec0005 },
	{ .index = CORTEX_PANIC_REGISTER_R6, .val = 0xecec0006 },
	{ .index = CORTEX_PANIC_REGISTER_R7, .val = 0xecec0007 },
	{ .index = CORTEX_PANIC_REGISTER_R8, .val = 0xecec0008 },
	{ .index = CORTEX_PANIC_REGISTER_R9, .val = 0xecec0009 },
	{ .index = CORTEX_PANIC_REGISTER_R10, .val = 0xecec000a },
	{ .index = CORTEX_PANIC_REGISTER_R11, .val = 0xecec000b },
};

test_static int test_exception_panic_registers(void)
{
	if (IS_ENABLED(CORE_CORTEX_M)) {
		asm volatile("ldr r0, =0xecec0000\n"
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
	} else if (IS_ENABLED(CORE_CORTEX_M0)) {
		asm volatile("ldr r1, =0xecec0001\n"
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
	}
	__builtin_unreachable();
}

test_static void run_test_step1(void)
{
	ccprintf("Step 1: Panic\n");
	system_set_scratchpad(TEST_STATE_MASK(TEST_STATE_STEP_2));
	RUN_TEST(test_exception_panic_registers);
}

test_static int run_test_step2(void)
{
	struct panic_data *data;
	int i;

	ccprintf("Step 2: Read panic data\n");
	data = panic_get_data();
	for (i = 0; i < ARRAY_SIZE(EXPECTED); i++) {
		TEST_EQ(EXPECTED[i].val, data->cm.regs[EXPECTED[i].index],
			"%04x");
		cflush();
	}
	return EC_SUCCESS;
}

void test_run_step(uint32_t state)
{
	int ret;

	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1))
		run_test_step1();
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		ret = run_test_step2();
		if (ret == EC_SUCCESS)
			test_reboot_to_next_step(TEST_STATE_PASSED);
		else
			test_reboot_to_next_step(TEST_STATE_FAILED);
	}
}

int task_test(void *unused)
{
	test_run_multistep();
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	crec_msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
