/* Copyright 2014 The ChromiumOS Authors
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
static int cmd_to_fail;

void battery_compensate_params(struct batt_params *batt)
{
}

void board_battery_compensate_params(struct batt_params *batt)
{
}

static void reset_counters(int first, int last)
{
	read_count = write_count = 0;
	fail_on_first = first;
	fail_on_last = last;
}

static void reset_and_fail_on(int first, int last, int cmd)
{
	/* We're not initializing the fake battery, so everything reads zero */
	memset(&batt, 0, sizeof(typeof(batt)));
	cmd_to_fail = cmd;
	reset_counters(first, last);
}

/* Mocked functions */
int sb_read(int cmd, int *param)
{
	read_count++;
	if (read_count >= fail_on_first && read_count <= fail_on_last)
		return EC_ERROR_UNKNOWN;

	if (cmd == cmd_to_fail)
		return EC_ERROR_UNKNOWN;

	return i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS, cmd, param);
}

int sb_write(int cmd, int param)
{
	write_count++;
	return i2c_write16(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS, cmd, param);
}

/* Tests */
static int test_param_failures(void)
{
	int i, num_reads;

	/* No failures */
	reset_and_fail_on(0, 0, -1);
	battery_get_params(&batt);
	TEST_ASSERT(batt.flags & BATT_FLAG_RESPONSIVE);
	TEST_ASSERT(!(batt.flags & BATT_FLAG_BAD_ANY));

	/* Save the max number of reads. */
	num_reads = read_count;

	/* Just a single failure */
	for (i = 1; i <= num_reads; i++) {
		reset_and_fail_on(i, i, -1);
		battery_get_params(&batt);
		TEST_ASSERT(batt.flags & BATT_FLAG_BAD_ANY);
		TEST_ASSERT(batt.flags & BATT_FLAG_RESPONSIVE);
	}

	/* Once it fails, it keeps failing */
	for (i = 1; i <= num_reads; i++) {
		reset_and_fail_on(i, num_reads, -1);
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

/**
 * Test if battery_get_params sets a flag properly for a SB command.
 *
 * @param cmd   SB command to fail.
 * @param flag  Flag expected to be set when <cmd> fails.
 * @return  EC_SUCCESS
 */
static int test_flag(int cmd, int flag)
{
	reset_and_fail_on(0, 0, cmd);
	battery_get_params(&batt);
	TEST_ASSERT(batt.flags & flag);
	TEST_ASSERT(!((batt.flags & ~flag) & BATT_FLAG_BAD_ANY));
	TEST_ASSERT(batt.flags & BATT_FLAG_RESPONSIVE);

	/*
	 * When SB_CHARGING_VOLTAGE, SB_CHARGING_CURRENT, or
	 * SB_RELATIVE_STATE_OF_CHARGE fails, WANT_CHARGE should be cleared.
	 */
	switch (cmd) {
	case SB_RELATIVE_STATE_OF_CHARGE:
	case SB_CHARGING_VOLTAGE:
	case SB_CHARGING_CURRENT:
		TEST_ASSERT(!(batt.flags & BATT_FLAG_WANT_CHARGE));
		TEST_ASSERT(batt.desired_voltage == 0);
		TEST_ASSERT(batt.desired_current == 0);
		break;
	default:
		TEST_ASSERT(batt.flags & BATT_FLAG_WANT_CHARGE);
		TEST_ASSERT(batt.desired_voltage == 100);
		TEST_ASSERT(batt.desired_current == 100);
	}

	/*
	 * Failure is recovered. <flag> should be cleared. WANT_CHARGE should be
	 * set.
	 */
	cmd_to_fail = -1;
	battery_get_params(&batt);
	TEST_ASSERT(!(batt.flags & flag));
	TEST_ASSERT(batt.flags & BATT_FLAG_WANT_CHARGE);

	return EC_SUCCESS;
}

static int test_flags(void)
{
	sb_write(SB_CHARGING_VOLTAGE, 100);
	sb_write(SB_CHARGING_CURRENT, 100);
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 50);

	/* Test each command-flag pair. */
	test_flag(SB_TEMPERATURE, BATT_FLAG_BAD_TEMPERATURE);
	test_flag(SB_RELATIVE_STATE_OF_CHARGE, BATT_FLAG_BAD_STATE_OF_CHARGE);
	test_flag(SB_VOLTAGE, BATT_FLAG_BAD_VOLTAGE);
	test_flag(SB_CURRENT, BATT_FLAG_BAD_CURRENT);
	test_flag(SB_AVERAGE_CURRENT, BATT_FLAG_BAD_AVERAGE_CURRENT);
	test_flag(SB_CHARGING_VOLTAGE, BATT_FLAG_BAD_DESIRED_VOLTAGE);
	test_flag(SB_CHARGING_CURRENT, BATT_FLAG_BAD_DESIRED_CURRENT);
	test_flag(SB_REMAINING_CAPACITY, BATT_FLAG_BAD_REMAINING_CAPACITY);
	test_flag(SB_FULL_CHARGE_CAPACITY, BATT_FLAG_BAD_FULL_CAPACITY);
	test_flag(SB_BATTERY_STATUS, BATT_FLAG_BAD_STATUS);

	/*
	 * Volatile flags should be cleared and other flags should be preserved.
	 */
	reset_and_fail_on(0, 0, -1);
	batt.flags |= BATT_FLAG_BAD_TEMPERATURE;
	batt.flags |= BIT(31);
	battery_get_params(&batt);
	TEST_ASSERT(batt.flags & BIT(31));
	TEST_ASSERT(!(batt.flags & BATT_FLAG_BAD_ANY));

	/*
	 * All reads succeed. BATT_FLAG_RESPONSIVE should be set. Then, all
	 * reads fail. BATT_FLAG_RESPONSIVE should be cleared.
	 */
	reset_and_fail_on(0, 0, -1);
	battery_get_params(&batt);
	TEST_ASSERT(batt.flags & BATT_FLAG_RESPONSIVE);

	reset_counters(1, read_count);
	battery_get_params(&batt);
	TEST_ASSERT(!(batt.flags & BATT_FLAG_RESPONSIVE));

	/* Test WANT_CHARGE is explicitly cleared. */
	reset_and_fail_on(0, 0, SB_RELATIVE_STATE_OF_CHARGE);
	batt.flags |= BATT_FLAG_WANT_CHARGE;
	battery_get_params(&batt);
	TEST_ASSERT(!(batt.flags & BATT_FLAG_WANT_CHARGE));

	return EC_SUCCESS;
}

static int test_full_state_of_charge(void)
{
	/*
	 * When SoC is full, BATT_FLAG_WANT_CHARGE should be cleared and
	 * desired voltage and current are also cleared.
	 */
	sb_write(SB_CHARGING_VOLTAGE, 100);
	sb_write(SB_CHARGING_CURRENT, 100);
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 100);

	reset_and_fail_on(0, 0, -1);
	battery_get_params(&batt);
	TEST_ASSERT(!(batt.flags & BATT_FLAG_WANT_CHARGE));
	TEST_ASSERT(batt.desired_voltage == 0);
	TEST_ASSERT(batt.desired_current == 0);
	TEST_ASSERT(batt.state_of_charge == 100);

	return EC_SUCCESS;
}

static int test_voltage(void)
{
	sb_write(SB_VOLTAGE, 100);
	reset_and_fail_on(0, 0, -1);

	battery_get_params(&batt);
	TEST_ASSERT(batt.voltage == 100);

	return EC_SUCCESS;
}

static int test_current(void)
{
	/* Test positive (charge) current. */
	sb_write(SB_CURRENT, 100);
	reset_and_fail_on(0, 0, -1);
	battery_get_params(&batt);
	TEST_ASSERT(batt.current == 100);

	/* Test negative (discharge) current. */
	sb_write(SB_CURRENT, -100);
	reset_and_fail_on(0, 0, -1);
	battery_get_params(&batt);
	TEST_ASSERT(batt.current == -100);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_param_failures);
	RUN_TEST(test_flags);
	RUN_TEST(test_full_state_of_charge);
	RUN_TEST(test_voltage);
	RUN_TEST(test_current);

	test_print_result();
}
