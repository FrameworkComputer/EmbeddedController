/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cpu.h"
#include "math.h"
#include "registers.h"
#include "task.h"
#include "test_util.h"
#include "time.h"

#if defined(CHIP_FAMILY_STM32F4) || defined(CHIP_FAMILY_STM32H7)
#define FPU_IRQ STM32_IRQ_FPU
#else
/* Value to make compilation succeed for chips that don't support FPU
 * interrupts.
 */
#define FPU_IRQ -1
#endif

static volatile uint32_t fpscr;
static volatile bool fpu_irq_handled;

static uint32_t _read_fpscr(void)
{
	uint32_t val;

	asm volatile("vmrs %0, fpscr" : "=r"(val));
	return val;
}

static void clear_fpscr(void)
{
	fpscr = _read_fpscr() & ~FPU_FPSCR_EXC_FLAGS;

	asm volatile("vmsr fpscr, %0" : : "r"(fpscr));
}

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

	fpu_irq_handled = true;
}

void read_fpscr(void)
{
	if (FPU_IRQ != -1) {
		/*
		 * On STM32H7 FPU interrupt is not triggered (see errata ES0392
		 * Rev 8, 2.1.2 Cortex-M7 FPU interrupt not present on NVIC line
		 * 81), so trigger it from software.
		 */
		if (IS_ENABLED(CHIP_FAMILY_STM32H7)) {
			task_trigger_irq(FPU_IRQ);
		}

		while (!fpu_irq_handled) {
		}
		return;
	}
	fpscr = _read_fpscr();
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

	clear_fpscr();
	fpu_irq_handled = false;

	result = divf(1.40130e-45f, 2.0f);

	TEST_ASSERT(result == 0.0f);

	read_fpscr();

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

	clear_fpscr();
	fpu_irq_handled = false;

	result = divf(3.40282e38f, 0.5f);

	TEST_ASSERT(isinf(result));

	read_fpscr();

	TEST_ASSERT(fpscr & FPU_FPSCR_OFC);

	return EC_SUCCESS;
}

/* Expect Division By Zero exception when 1.0f/0.0f. */
test_static int test_cortexm_fpu_division_by_zero(void)
{
	float result;

	clear_fpscr();
	fpu_irq_handled = false;

	result = divf(1.0f, 0.0f);

	TEST_ASSERT(isinf(result));

	read_fpscr();

	TEST_ASSERT(fpscr & FPU_FPSCR_DZC);

	return EC_SUCCESS;
}

/* Expect Invalid Operation when trying to get square root of -1.0f. */
test_static int test_cortexm_fpu_invalid_operation(void)
{
	float result;

	clear_fpscr();
	fpu_irq_handled = false;

	result = sqrtf(-1.0f);

	TEST_ASSERT(isnan(result));

	read_fpscr();

	TEST_ASSERT(fpscr & FPU_FPSCR_IOC);

	return EC_SUCCESS;
}

/* Expect Inexact bit to be set when performing 2.0f/3.0f. */
test_static int test_cortexm_fpu_inexact(void)
{
	float result;

	clear_fpscr();
	fpu_irq_handled = false;

	result = divf(2.0f, 3.0f);

	/* Check if result is not NaN nor infinity. */
	TEST_ASSERT(!isnan(result) && !isinf(result));

	fpscr = _read_fpscr();

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
