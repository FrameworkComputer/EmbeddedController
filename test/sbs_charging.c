/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test lid switch.
 */

#include "battery_smart.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "test_util.h"
#include "util.h"

#define WAIT_CHARGER_TASK 500
#define BATTERY_DETACH_DELAY 35000

static int mock_chipset_state = CHIPSET_STATE_ON;
static int is_shutdown;
static int is_force_discharge;
static int is_hibernated;

void chipset_force_shutdown(void)
{
	is_shutdown = 1;
}

int chipset_in_state(int state_mask)
{
	return state_mask & mock_chipset_state;
}

int board_discharge_on_ac(int enabled)
{
	is_force_discharge = enabled;
	return EC_SUCCESS;
}

void system_hibernate(int sec, int usec)
{
	is_hibernated = 1;
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
	gpio_set_level(GPIO_AC_PRESENT, 0);
}

static int wait_charging_state(void)
{
	enum charge_state state;
	task_wake(TASK_ID_CHARGER);
	msleep(WAIT_CHARGER_TASK);
	state = charge_get_state();
	ccprintf("[CHARGING TEST] state = %d\n", state);
	return state;
}

static int charge_control(enum ec_charge_control_mode mode)
{
	struct ec_params_charge_control params;
	params.mode = mode;
	return test_send_host_command(EC_CMD_CHARGE_CONTROL, 1, &params,
				      sizeof(params), NULL, 0);
}

static int test_charge_state(void)
{
	enum charge_state state;

	state = wait_charging_state();
	/* Plug AC, charging at 1000mAh */
	ccprintf("[CHARGING TEST] AC on\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	sb_write(SB_CURRENT, 1000);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);

	/* Detach battery, charging error */
	ccprintf("[CHARGING TEST] Detach battery\n");
	TEST_ASSERT(test_detach_i2c(I2C_PORT_BATTERY, BATTERY_ADDR) ==
		    EC_SUCCESS);
	msleep(BATTERY_DETACH_DELAY);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_ERROR);

	/* Attach battery again, charging */
	ccprintf("[CHARGING TEST] Attach battery\n");
	test_attach_i2c(I2C_PORT_BATTERY, BATTERY_ADDR);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);

	/* Unplug AC, discharging at 1000mAh */
	ccprintf("[CHARGING TEST] AC off\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);

	/* Discharging overtemp */
	ccprintf("[CHARGING TEST] AC off, batt temp = 90 C\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);

	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(90));
	state = wait_charging_state();
	TEST_ASSERT(is_shutdown);
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(40));

	/* Force idle */
	ccprintf("[CHARGING TEST] AC on, force idle\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	sb_write(SB_CURRENT, 1000);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	charge_control(CHARGE_CONTROL_IDLE);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_IDLE);
	charge_control(CHARGE_CONTROL_NORMAL);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);

	/* Force discharge */
	ccprintf("[CHARGING TEST] AC on, force discharge\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	sb_write(SB_CURRENT, 1000);
	charge_control(CHARGE_CONTROL_DISCHARGE);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_IDLE);
	TEST_ASSERT(is_force_discharge);
	charge_control(CHARGE_CONTROL_NORMAL);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	TEST_ASSERT(!is_force_discharge);

	return EC_SUCCESS;
}

static int test_low_battery(void)
{
	ccprintf("[CHARGING TEST] Low battery with AC\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	is_hibernated = 0;
	sb_write(SB_CURRENT, 1000);
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 2);
	wait_charging_state();
	mock_chipset_state = CHIPSET_STATE_SOFT_OFF;
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	TEST_ASSERT(!is_hibernated);

	ccprintf("[CHARGING TEST] Low battery shutdown S0->S5\n");
	mock_chipset_state = CHIPSET_STATE_ON;
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	hook_notify(HOOK_CHIPSET_STARTUP);
	gpio_set_level(GPIO_AC_PRESENT, 0);
	is_hibernated = 0;
	sb_write(SB_CURRENT, -1000);
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 2);
	wait_charging_state();
	mock_chipset_state = CHIPSET_STATE_SOFT_OFF;
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	TEST_ASSERT(is_hibernated);

	ccprintf("[CHARGING TEST] Low battery shutdown S5\n");
	is_hibernated = 0;
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 10);
	wait_charging_state();
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 2);
	wait_charging_state();
	TEST_ASSERT(is_hibernated);

	ccprintf("[CHARGING TEST] Low battery AP shutdown\n");
	is_shutdown = 0;
	mock_chipset_state = CHIPSET_STATE_ON;
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 10);
	gpio_set_level(GPIO_AC_PRESENT, 1);
	sb_write(SB_CURRENT, 1000);
	wait_charging_state();
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 2);
	wait_charging_state();
	usleep(32 * SECOND);
	wait_charging_state();
	TEST_ASSERT(is_shutdown);

	return EC_SUCCESS;
}

static int test_batt_fake(void)
{
	ccprintf("[CHARGING TEST] Fake battery command\n");
	mock_chipset_state = CHIPSET_STATE_ON;
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	hook_notify(HOOK_CHIPSET_STARTUP);
	gpio_set_level(GPIO_AC_PRESENT, 0);
	is_hibernated = 0;
	sb_write(SB_CURRENT, -1000);
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 30);
	wait_charging_state();
	UART_INJECT("battfake 2\n");
	msleep(50);
	mock_chipset_state = CHIPSET_STATE_SOFT_OFF;
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	TEST_ASSERT(is_hibernated);

	return EC_SUCCESS;
}

void run_test(void)
{
	test_setup();

	RUN_TEST(test_charge_state);
	RUN_TEST(test_low_battery);
	RUN_TEST(test_batt_fake);

	test_print_result();
}
