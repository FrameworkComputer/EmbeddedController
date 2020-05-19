/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test the logic of battery_get_params() to be sure it sets the correct flags
 * when i2c reads fail.
 */

#include "battery.h"
#include "battery_smart.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "test_util.h"
#include "util.h"

/* Test state */
static int fail_on_first, fail_on_last;
static int read_count, write_count;
struct batt_params batt;


void battery_compensate_params(struct batt_params *batt)
{
}

void board_battery_compensate_params(struct batt_params *batt)
{
}

static void reset_and_fail_on(int first, int last)
{
	/* We're not initializing the fake battery, so everything reads zero */
	memset(&batt, 0, sizeof(typeof(batt)));
	read_count = write_count = 0;
	fail_on_first = first;
	fail_on_last = last;
}

/* Mocked functions */
int sb_read(int cmd, int *param)
{
	read_count++;
	if (read_count >= fail_on_first && read_count <= fail_on_last)
		return EC_ERROR_UNKNOWN;

	return i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
			  cmd, param);
}
int sb_write(int cmd, int param)
{
	write_count++;
	return i2c_write16(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
			   cmd, param);
}


/* Tests */
static int test_param_failures(void)
{
	int i, num_reads;

	/* No failures */
	reset_and_fail_on(0, 0);
	battery_get_params(&batt);
	TEST_ASSERT(batt.flags & BATT_FLAG_RESPONSIVE);
	TEST_ASSERT(!(batt.flags & BATT_FLAG_BAD_ANY));
	num_reads = read_count;

	/* Just a single failure */
	for (i = 1; i <= num_reads; i++) {
		reset_and_fail_on(i, i);
		battery_get_params(&batt);
		TEST_ASSERT(batt.flags & BATT_FLAG_BAD_ANY);
		TEST_ASSERT(batt.flags & BATT_FLAG_RESPONSIVE);
	}

	/* Once it fails, it keeps failing */
	for (i = 1; i <= num_reads; i++) {
		reset_and_fail_on(i, num_reads);
		battery_get_params(&batt);
		TEST_ASSERT(batt.flags & BATT_FLAG_BAD_ANY);
		if (i == 1)
			/* If every read fails, it's not responsive */
			TEST_ASSERT(!(batt.flags & BATT_FLAG_RESPONSIVE));
		else
			TEST_ASSERT(batt.flags & BATT_FLAG_RESPONSIVE);
	}

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_param_failures);

	test_print_result();
}
