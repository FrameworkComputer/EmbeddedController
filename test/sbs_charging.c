/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test lid switch.
 */

#include "battery_pack.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "smart_battery.h"
#include "task.h"
#include "test_util.h"
#include "util.h"

#define WAIT_CHARGER_TASK 500

static int mock_ac_present = 1;
static int mock_chipset_state = CHIPSET_STATE_ON;
static int is_shutdown;

/* Mock GPIOs */
int gpio_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_AC_PRESENT)
		return mock_ac_present;
	return 0;
}

void chipset_force_shutdown(void)
{
	is_shutdown = 1;
}

int chipset_in_state(int state_mask)
{
	return state_mask & mock_chipset_state;
}

/* Setup init condition */
static void test_setup(void)
{
	const struct battery_info *bat_info = battery_get_info();

	/* 50% of charge */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 50);
	sb_write(SB_ABSOLUTE_STATE_OF_CHARGE, 50);
	/* 25 degree Celsius */
	sb_write(SB_TEMPERATURE, 250 + 2731);
	/* Normal voltage */
	sb_write(SB_VOLTAGE, bat_info->voltage_normal);
	sb_write(SB_CHARGING_VOLTAGE, bat_info->voltage_max);
	sb_write(SB_CHARGING_CURRENT, 4000);
	/* Discharging at 100mAh */
	sb_write(SB_CURRENT, -100);
	/* Unplug AC */
	mock_ac_present = 0;
}

static int wait_charging_state(void)
{
	enum power_state state;
	task_wake(TASK_ID_CHARGER);
	msleep(WAIT_CHARGER_TASK);
	state = charge_get_state();
	ccprintf("[CHARGING TEST] state = %d\n", state);
	return state;
}

static int test_charge_state(void)
{
	enum power_state state;

	state = wait_charging_state();
	/* Plug AC, charging at 1000mAh */
	ccprintf("[CHARGING TEST] AC on\n");
	mock_ac_present = 1;
	sb_write(SB_CURRENT, 1000);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);

	/* Unplug AC, discharging at 1000mAh */
	ccprintf("[CHARGING TEST] AC off\n");
	mock_ac_present = 0;
	sb_write(SB_CURRENT, -1000);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);

	/* Discharging overtemp */
	ccprintf("[CHARGING TEST] AC off, batt temp = 90 C\n");
	mock_ac_present = 0;
	sb_write(SB_CURRENT, -1000);

	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(90));
	state = wait_charging_state();
	TEST_ASSERT(is_shutdown);
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);

	return EC_SUCCESS;
}

void run_test(void)
{
	test_setup();

	RUN_TEST(test_charge_state);

	test_print_result();
}
