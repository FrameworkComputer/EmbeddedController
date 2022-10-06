/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_util.h"

#include "cpu.h"
#include "math.h"
#include "registers.h"
#include "task.h"
#include "time.h"

static volatile uint32_t fpscr;

/* Override default FPU interrupt handler. */
void __keep fpu_irq(uint32_t excep_lr, uint32_t excep_sp)
{
	/*
	 * Get address of exception FPU exception frame. FPCAR register points
	 * to the beginning of allocated FPU exception frame on the stack.
	 */
	uint32_t *fpu_state = (uint32_t *)CPU_FPU_FPCAR;

	fpscr = fpu_state[FPU_IDX_REG_FPSCR];

	/* Clear FPSCR on stack. */
	fpu_state[FPU_IDX_REG_FPSCR] &= ~FPU_FPSCR_EXC_FLAGS;
}

/* Performs division without casting to double. */
static float divf(float a, float b)
{
	float result;

	asm volatile("fdivs %0, %1, %2" : "=w"(result) : "w"(a), "w"(b));

	return result;
}

/*
 * Expect underflow when dividing the smallest number that can be represented
 * using floats.
 */
test_static int test_cortexm_fpu_underflow(void)
{
	float result;

	fpscr = 0;
	result = divf(1.40130e-45f, 2.0f);

	/*
	 * On STM32H7 FPU interrupt is not triggered (see errata ES0392 Rev 8,
	 * 2.1.2 Cortex-M7 FPU interrupt not present on NVIC line 81), so
	 * trigger it from software.
	 */
	if (IS_ENABLED(CHIP_FAMILY_STM32H7))
		task_trigger_irq(STM32_IRQ_FPU);

	TEST_ASSERT(result == 0.0f);

	TEST_ASSERT(fpscr & FPU_FPSCR_UFC);

	return EC_SUCCESS;
}

/*
 * Expect overflow when dividing the highest number that can be represented
 * using floats by number smaller than < 1.0f.
 */
test_static int test_cortexm_fpu_overflow(void)
{
	float result;

	fpscr = 0;
	result = divf(3.40282e38f, 0.5f);

	/*
	 * On STM32H7 FPU interrupt is not triggered (see errata ES0392 Rev 8,
	 * 2.1.2 Cortex-M7 FPU interrupt not present on NVIC line 81), so
	 * trigger it from software.
	 */
	if (IS_ENABLED(CHIP_FAMILY_STM32H7))
		task_trigger_irq(STM32_IRQ_FPU);

	TEST_ASSERT(isinf(result));

	TEST_ASSERT(fpscr & FPU_FPSCR_OFC);

	return EC_SUCCESS;
}

/* Expect Division By Zero exception when 1.0f/0.0f. */
test_static int test_cortexm_fpu_division_by_zero(void)
{
	float result;

	fpscr = 0;
	result = divf(1.0f, 0.0f);

	/*
	 * On STM32H7 FPU interrupt is not triggered (see errata ES0392 Rev 8,
	 * 2.1.2 Cortex-M7 FPU interrupt not present on NVIC line 81), so
	 * trigger it from software.
	 */
	if (IS_ENABLED(CHIP_FAMILY_STM32H7))
		task_trigger_irq(STM32_IRQ_FPU);

	TEST_ASSERT(isinf(result));

	TEST_ASSERT(fpscr & FPU_FPSCR_DZC);

	return EC_SUCCESS;
}

/* Expect Invalid Operation when trying to get square root of -1.0f. */
test_static int test_cortexm_fpu_invalid_operation(void)
{
	float result;

	fpscr = 0;
	result = sqrtf(-1.0f);

	/*
	 * On STM32H7 FPU interrupt is not triggered (see errata ES0392 Rev 8,
	 * 2.1.2 Cortex-M7 FPU interrupt not present on NVIC line 81), so
	 * trigger it from software.
	 */
	if (IS_ENABLED(CHIP_FAMILY_STM32H7))
		task_trigger_irq(STM32_IRQ_FPU);

	TEST_ASSERT(isnan(result));

	TEST_ASSERT(fpscr & FPU_FPSCR_IOC);

	return EC_SUCCESS;
}

/* Expect Inexact bit to be set when performing 2.0f/3.0f. */
test_static int test_cortexm_fpu_inexact(void)
{
	float result;

	fpscr = 0;
	result = divf(2.0f, 3.0f);

	/*
	 * Inexact bit doesn't generate interrupt, so we will trigger it from
	 * software.
	 */
	task_trigger_irq(STM32_IRQ_FPU);

	/* Check if result is not NaN nor infinity. */
	TEST_ASSERT(!isnan(result) && !isinf(result));

	TEST_ASSERT(fpscr & FPU_FPSCR_IXC);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	if (IS_ENABLED(CONFIG_FPU)) {
		RUN_TEST(test_cortexm_fpu_underflow);
		RUN_TEST(test_cortexm_fpu_overflow);
		RUN_TEST(test_cortexm_fpu_division_by_zero);
		RUN_TEST(test_cortexm_fpu_invalid_operation);
		RUN_TEST(test_cortexm_fpu_inexact);
	}

	test_print_result();
}
