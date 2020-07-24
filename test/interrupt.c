/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test interrupt support of EC emulator.
 */
#include <stdio.h>

#include "common.h"
#include "console.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int main_count;
static int has_error;
static int interrupt_count;

/* period between 50us and 3.2ms */
#define PERIOD_US(num) (((num % 64) + 1) * 50)

void my_isr(void)
{
	int i = main_count;
	udelay(3 * PERIOD_US(prng_no_seed()));
	if (i != main_count || !in_interrupt_context())
		has_error = 1;
	interrupt_count++;
}

static volatile uint32_t enable_ready_reg;

static void set_ready_bit(void)
{
	if (enable_ready_reg & BIT(0))
		enable_ready_reg |= BIT(1);
}

void interrupt_generator(void)
{
	while (1) {
		udelay(3 * PERIOD_US(prng_no_seed()));
		task_trigger_test_interrupt(my_isr);
		task_trigger_test_interrupt(set_ready_bit);
	}
}

static int interrupt_test(void)
{
	timestamp_t deadline = get_time();
	deadline.val += SECOND / 2;
	while (!timestamp_expired(deadline, NULL))
		++main_count;

	ccprintf("Interrupt count: %d\n", interrupt_count);
	ccprintf("Main thread tick: %d\n", main_count);

	TEST_ASSERT(!has_error);
	TEST_ASSERT(!in_interrupt_context());

	return EC_SUCCESS;
}

static int interrupt_disable_test(void)
{
	timestamp_t deadline = get_time();
	int start_int_cnt, end_int_cnt;
	deadline.val += SECOND / 2;

	interrupt_disable();
	start_int_cnt = interrupt_count;
	while (!timestamp_expired(deadline, NULL))
		;
	end_int_cnt = interrupt_count;
	interrupt_enable();

	TEST_ASSERT(start_int_cnt == end_int_cnt);

	return EC_SUCCESS;
}

static int test_wait_for_ready(void)
{
	wait_for_ready(&enable_ready_reg, BIT(0), BIT(1));
	TEST_EQ(enable_ready_reg, BIT(0) | BIT(1), "%x");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(interrupt_test);
	RUN_TEST(interrupt_disable_test);
	RUN_TEST(test_wait_for_ready);

	test_print_result();
}
