/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "task.h"
#include "test_util.h"

static uint32_t interrupt_disable_count;
static uint32_t interrupt_enable_count;

/** Mock implementation of interrupt_disable. */
void interrupt_disable(void)
{
	++interrupt_disable_count;
}

/** Mock implementation of interrupt_enable. */
void interrupt_enable(void)
{
	++interrupt_enable_count;
}

static int test_simple_lock_unlock(void)
{
	uint32_t key = irq_lock();

	irq_unlock(key);

	TEST_EQ(interrupt_disable_count, 1, "%u");
	TEST_EQ(interrupt_enable_count, 1, "%u");

	return EC_SUCCESS;
}

static int test_unlock_when_all_keys_removed(void)
{
	uint32_t key0 = irq_lock();
	uint32_t key1 = irq_lock();

	TEST_EQ(interrupt_disable_count, 2, "%u");

	irq_unlock(key1);

	TEST_EQ(interrupt_enable_count, 0, "%u");

	irq_unlock(key0);

	TEST_EQ(interrupt_enable_count, 1, "%u");

	return EC_SUCCESS;
}

static int test_unlock_from_root_key(void)
{
	uint32_t key0 = irq_lock();
	uint32_t key1 = irq_lock();

	TEST_NE(key0, key1, "%u");
	TEST_EQ(interrupt_disable_count, 2, "%u");

	irq_unlock(key0);
	TEST_EQ(interrupt_enable_count, 1, "%u");

	return EC_SUCCESS;
}

void before_test(void)
{
	interrupt_disable_count = 0;
	interrupt_enable_count = 0;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_simple_lock_unlock);
	RUN_TEST(test_unlock_when_all_keys_removed);
	RUN_TEST(test_unlock_from_root_key);

	test_print_result();
}
