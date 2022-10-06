/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test charge_state_v2 behavior
 */

#include "battery_smart.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "math_util.h"
#include "power.h"
#include "task.h"
#include "test_util.h"
#include "util.h"

#define WAIT_CHARGER_TASK 600
#define BATTERY_DETACH_DELAY 35000

enum ec_charge_control_mode get_chg_ctrl_mode(void);

static int mock_chipset_state = CHIPSET_STATE_ON;
static int is_shutdown;
static int is_force_discharge;
static int is_hibernated;
static int override_voltage, override_current, override_usec;
static int display_soc;
static int is_full;

/* The simulation doesn't really hibernate, so we must reset this ourselves */
extern timestamp_t shutdown_target_time;

static void reset_mocks(void)
{
	mock_chipset_state = CHIPSET_STATE_ON;
	is_shutdown = is_force_discharge = is_hibernated = 0;
	override_voltage = override_current = override_usec = 0;
	shutdown_target_time.val = 0ULL;
	is_full = 0;
}

int board_cut_off_battery(void)
{
	return EC_SUCCESS;
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	is_shutdown = 1;
	mock_chipset_state = CHIPSET_STATE_HARD_OFF;
}

int chipset_in_state(int state_mask)
{
	return state_mask & mock_chipset_state;
}

int chipset_in_or_transitioning_to_state(int state_mask)
{
	return state_mask & mock_chipset_state;
}

enum power_state power_get_state(void)
{
	if (is_shutdown)
		return POWER_S5;
	else if (is_hibernated)
		return POWER_G3;
	else
		return POWER_S0;
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

int charger_profile_override(struct charge_state_data *curr)
{
	if (override_voltage)
		curr->requested_voltage = override_voltage;
	if (override_current)
		curr->requested_current = override_current;

	if (override_usec)
		return override_usec;

	/* Don't let it sleep a whole minute when the AP is off */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return CHARGE_POLL_PERIOD_LONG;

	return 0;
}

static uint32_t meh;
enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	if (param == CS_PARAM_CUSTOM_PROFILE_MIN) {
		*value = meh;
		return EC_RES_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
}
enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	if (param == CS_PARAM_CUSTOM_PROFILE_MIN) {
		meh = value;
		return EC_RES_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
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
	struct ec_params_charge_control p;

	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = mode;
	p.sustain_soc.lower = -1;
	p.sustain_soc.upper = -1;
	return test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				      NULL, 0);
}

__override int charge_get_display_charge(void)
{
	return display_soc;
}

__override int calc_is_full(void)
{
	return is_full;
}

/* Setup init condition */
static void test_setup(int on_ac)
{
	const struct battery_info *bat_info = battery_get_info();

	reset_mocks();

	/* 50% of charge */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 50);
	sb_write(SB_ABSOLUTE_STATE_OF_CHARGE, 50);
	/* full charge capacity in mAh */
	sb_write(SB_FULL_CHARGE_CAPACITY, 0xf000);
	/* 25 degree Celsius */
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(25));
	/* battery pack voltage */
	sb_write(SB_VOLTAGE, bat_info->voltage_normal);
	/* desired charging voltage/current */
	sb_write(SB_CHARGING_VOLTAGE, bat_info->voltage_max);
	sb_write(SB_CHARGING_CURRENT, 4000);

	/* battery pack current is positive when charging */
	if (on_ac) {
		sb_write(SB_CURRENT, 1000);
		gpio_set_level(GPIO_AC_PRESENT, 1);
	} else {
		sb_write(SB_CURRENT, -100);
		gpio_set_level(GPIO_AC_PRESENT, 0);
	}

	/* Reset the charger state to initial state */
	charge_control(CHARGE_CONTROL_NORMAL);

	/* Let things stabilize */
	wait_charging_state();
}

/* Host Event helpers */
static int ev_is_set(int event)
{
	return host_get_events() & EC_HOST_EVENT_MASK(event);
}
static int ev_is_clear(int event)
{
	return !ev_is_set(event);
}
static void ev_clear(int event)
{
	host_clear_events(EC_HOST_EVENT_MASK(event));
}

static int test_charge_state(void)
{
	enum charge_state state;
	uint32_t flags;

	/* On AC */
	test_setup(1);

	ccprintf("[CHARGING TEST] AC on\n");

	/* Detach battery, charging error */
	ccprintf("[CHARGING TEST] Detach battery\n");
	TEST_ASSERT(test_detach_i2c(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS) ==
		    EC_SUCCESS);
	msleep(BATTERY_DETACH_DELAY);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_ERROR);

	/* Attach battery again, charging */
	ccprintf("[CHARGING TEST] Attach battery\n");
	test_attach_i2c(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS);
	/* And changing full capacity should trigger a host event */
	ev_clear(EC_HOST_EVENT_BATTERY);
	sb_write(SB_FULL_CHARGE_CAPACITY, 0xeff0);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY));

	/* Unplug AC, discharging at 1000mAh */
	ccprintf("[CHARGING TEST] AC off\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	flags = charge_get_flags();
	TEST_ASSERT(!(flags & CHARGE_FLAG_EXTERNAL_POWER));
	TEST_ASSERT(!(flags & CHARGE_FLAG_FORCE_IDLE));

	/* Discharging waaaay overtemp is ignored */
	ccprintf("[CHARGING TEST] AC off, batt temp = 0xffff\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	sb_write(SB_TEMPERATURE, 0xffff);
	state = wait_charging_state();
	TEST_ASSERT(!is_shutdown);
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(40));

	/* Discharging overtemp */
	ccprintf("[CHARGING TEST] AC off, batt temp = 90 C\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);

	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(90));
	state = wait_charging_state();
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
	TEST_ASSERT(is_shutdown);
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(40));

	/* Force idle */
	ccprintf("[CHARGING TEST] AC on, force idle\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	sb_write(SB_CURRENT, 1000);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_FLAG_FORCE_IDLE));
	charge_control(CHARGE_CONTROL_IDLE);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_FORCED_IDLE);
	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(flags & CHARGE_FLAG_FORCE_IDLE);
	charge_control(CHARGE_CONTROL_NORMAL);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);

	/* Force discharge */
	ccprintf("[CHARGING TEST] AC on, force discharge\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	sb_write(SB_CURRENT, 1000);
	charge_control(CHARGE_CONTROL_DISCHARGE);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_FORCED_IDLE);
	TEST_ASSERT(is_force_discharge);
	charge_control(CHARGE_CONTROL_NORMAL);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	TEST_ASSERT(!is_force_discharge);

	return EC_SUCCESS;
}

static int test_low_battery(void)
{
	test_setup(1);

	ccprintf("[CHARGING TEST] Low battery with AC and positive current\n");
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 2);
	sb_write(SB_CURRENT, 1000);
	wait_charging_state();
	mock_chipset_state = CHIPSET_STATE_SOFT_OFF;
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	TEST_ASSERT(!is_hibernated);

	ccprintf("[CHARGING TEST] Low battery with AC and negative current\n");
	sb_write(SB_CURRENT, -1000);
	wait_charging_state();
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
	TEST_ASSERT(is_hibernated);

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
	wait_charging_state();
	/* after a while, the EC should hibernate */
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
	TEST_ASSERT(is_hibernated);

	ccprintf("[CHARGING TEST] Low battery shutdown S5\n");
	is_hibernated = 0;
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 10);
	wait_charging_state();
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, 2);
	wait_charging_state();
	/* after a while, the EC should hibernate */
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
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

static int test_deep_charge_battery(void)
{
	enum charge_state_v2 state_v2;
	const struct battery_info *bat_info = battery_get_info();

	test_setup(1);

	/* battery pack voltage bellow voltage_min */
	sb_write(SB_VOLTAGE, (bat_info->voltage_min - 200));
	wait_charging_state();
	state_v2 = charge_get_state_v2();
	TEST_ASSERT(state_v2 == ST_PRECHARGE);

	/*
	 * Battery voltage keep bellow voltage_min,
	 * precharge over time CONFIG_BATTERY_LOW_VOLTAGE_TIMEOUT
	 */
	usleep(CONFIG_BATTERY_LOW_VOLTAGE_TIMEOUT);
	state_v2 = charge_get_state_v2();
	TEST_ASSERT(state_v2 == ST_IDLE);

	/* recovery from a low voltage. */
	sb_write(SB_VOLTAGE, (bat_info->voltage_normal));
	wait_charging_state();
	state_v2 = charge_get_state_v2();
	TEST_ASSERT(state_v2 == ST_CHARGE);

	return EC_SUCCESS;
}
static int test_high_temp_battery(void)
{
	test_setup(1);

	ccprintf("[CHARGING TEST] High battery temperature shutdown\n");
	ev_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(90));
	wait_charging_state();
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
	TEST_ASSERT(is_shutdown);

	ccprintf("[CHARGING TEST] High battery temp S0->S5 hibernate\n");
	mock_chipset_state = CHIPSET_STATE_SOFT_OFF;
	wait_charging_state();
	TEST_ASSERT(is_hibernated);

	return EC_SUCCESS;
}

static int test_cold_battery_with_ac(void)
{
	test_setup(1);

	ccprintf("[CHARGING TEST] Cold battery no shutdown with AC\n");
	ev_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(-90));
	wait_charging_state();
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
	TEST_ASSERT(!is_shutdown);

	return EC_SUCCESS;
}

static int test_cold_battery_no_ac(void)
{
	test_setup(0);

	ccprintf("[CHARGING TEST] Cold battery shutdown when discharging\n");
	ev_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(-90));
	wait_charging_state();
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
	TEST_ASSERT(is_shutdown);

	return EC_SUCCESS;
}

static int test_external_funcs(void)
{
	int rv, temp;
	uint32_t flags;
	int state;

	/* Connect the AC */
	test_setup(1);

	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_FLAG_FORCE_IDLE));

	/* Invalid or do-nothing commands first */
	UART_INJECT("chg\n");
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_FLAG_FORCE_IDLE));

	UART_INJECT("chg blahblah\n");
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_FLAG_FORCE_IDLE));

	UART_INJECT("chg idle\n");
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_FLAG_FORCE_IDLE));

	UART_INJECT("chg idle blargh\n");
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_FLAG_FORCE_IDLE));

	/* Now let's force idle on and off */
	UART_INJECT("chg idle on\n");
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_FORCED_IDLE);
	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(flags & CHARGE_FLAG_FORCE_IDLE);

	UART_INJECT("chg idle off\n");
	wait_charging_state();
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_CHARGE);
	flags = charge_get_flags();
	TEST_ASSERT(flags & CHARGE_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_FLAG_FORCE_IDLE));

	/* and the rest */
	TEST_ASSERT(charge_get_state() == PWR_STATE_CHARGE);
	TEST_ASSERT(!charge_want_shutdown());
	TEST_ASSERT(charge_get_percent() == 50);
	temp = 0;
	rv = charge_get_battery_temp(0, &temp);
	TEST_ASSERT(rv == EC_SUCCESS);
	TEST_ASSERT(K_TO_C(temp) == 25);

	return EC_SUCCESS;
}

#define CHG_OPT1 0x2000
#define CHG_OPT2 0x4000
static int test_hc_charge_state(void)
{
	enum charge_state state;
	int i, rv, tmp;
	struct ec_params_charge_state params;
	struct ec_response_charge_state resp;

	/* Let's connect the AC again. */
	test_setup(1);

	/* Initialize the charger options with some nonzero value */
	TEST_ASSERT(charger_set_option(CHG_OPT1) == EC_SUCCESS);

	/* Get the state */
	memset(&resp, 0, sizeof(resp));
	params.cmd = CHARGE_STATE_CMD_GET_STATE;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(resp.get_state.ac);
	TEST_ASSERT(resp.get_state.chg_voltage);
	TEST_ASSERT(resp.get_state.chg_current);
	TEST_ASSERT(resp.get_state.chg_input_current);
	TEST_ASSERT(resp.get_state.batt_state_of_charge);

	/* Check all the params */
	for (i = 0; i < CS_NUM_BASE_PARAMS; i++) {
		/* Read it */
		memset(&resp, 0, sizeof(resp));
		params.cmd = CHARGE_STATE_CMD_GET_PARAM;
		params.get_param.param = i;
		rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					    sizeof(params), &resp,
					    sizeof(resp));
		TEST_ASSERT(rv == EC_RES_SUCCESS);
		if (i != CS_PARAM_LIMIT_POWER)
			TEST_ASSERT(resp.get_param.value);
		else
			TEST_ASSERT(!resp.get_param.value);

		/* Bump it up a bit */
		tmp = resp.get_param.value;
		switch (i) {
		case CS_PARAM_CHG_VOLTAGE:
		case CS_PARAM_CHG_CURRENT:
		case CS_PARAM_CHG_INPUT_CURRENT:
			tmp -= 128; /* Should be valid delta */
			break;
		case CS_PARAM_CHG_STATUS:
		case CS_PARAM_LIMIT_POWER:
			/* These ones can't be set */
			break;
		case CS_PARAM_CHG_OPTION:
			tmp = CHG_OPT2;
			break;
		}
		params.cmd = CHARGE_STATE_CMD_SET_PARAM;
		params.set_param.param = i;
		params.set_param.value = tmp;
		rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					    sizeof(params), &resp,
					    sizeof(resp));
		if (i == CS_PARAM_CHG_STATUS || i == CS_PARAM_LIMIT_POWER)
			TEST_ASSERT(rv == EC_RES_ACCESS_DENIED);
		else
			TEST_ASSERT(rv == EC_RES_SUCCESS);
		/* Allow the change to take effect */
		state = wait_charging_state();
		TEST_ASSERT(state == PWR_STATE_CHARGE);

		/* Read it back again*/
		memset(&resp, 0, sizeof(resp));
		params.cmd = CHARGE_STATE_CMD_GET_PARAM;
		params.get_param.param = i;
		rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					    sizeof(params), &resp,
					    sizeof(resp));
		TEST_ASSERT(rv == EC_RES_SUCCESS);
		TEST_ASSERT(resp.get_param.value == tmp);
	}

	/* And a custom profile param */
	meh = 0xdeadbeef;
	memset(&resp, 0, sizeof(resp));
	params.cmd = CHARGE_STATE_CMD_GET_PARAM;
	params.get_param.param = CS_PARAM_CUSTOM_PROFILE_MIN;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(resp.get_param.value == meh);
	params.cmd = CHARGE_STATE_CMD_SET_PARAM;
	params.set_param.param = CS_PARAM_CUSTOM_PROFILE_MIN;
	params.set_param.value = 0xc0def00d;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	/* Allow the change to take effect */
	state = wait_charging_state();
	TEST_ASSERT(meh == params.set_param.value);

	/* param out of range */
	params.cmd = CHARGE_STATE_CMD_GET_PARAM;
	params.get_param.param = CS_NUM_BASE_PARAMS;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);
	params.cmd = CHARGE_STATE_CMD_SET_PARAM;
	params.set_param.param = CS_NUM_BASE_PARAMS;
	params.set_param.value = 0x1000; /* random value */
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	/* command out of range */
	params.cmd = CHARGE_STATE_NUM_CMDS;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	/*
	 * We've screwed with the charger settings, so let the state machine
	 * reset itself before we stop.
	 */
	test_setup(0);
	test_setup(1);

	return EC_SUCCESS;
}

static int test_hc_current_limit(void)
{
	int rv, norm_current, lower_current;
	struct ec_params_charge_state cs_params;
	struct ec_response_charge_state cs_resp;
	struct ec_params_current_limit cl_params;

	/* On AC */
	test_setup(1);

	/* See what current the charger is delivering */
	cs_params.cmd = CHARGE_STATE_CMD_GET_STATE;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &cs_params,
				    sizeof(cs_params), &cs_resp,
				    sizeof(cs_resp));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	norm_current = cs_resp.get_state.chg_current;

	/* Lower it a bit */
	lower_current = norm_current - 256;
	cl_params.limit = lower_current;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 0, &cl_params,
				    sizeof(cl_params), 0, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	wait_charging_state();

	/* See that it's changed */
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &cs_params,
				    sizeof(cs_params), &cs_resp,
				    sizeof(cs_resp));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(lower_current == cs_resp.get_state.chg_current);

	/* Remove the limit */
	cl_params.limit = -1U;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 0, &cl_params,
				    sizeof(cl_params), 0, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	wait_charging_state();

	/* See that it's back */
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &cs_params,
				    sizeof(cs_params), &cs_resp,
				    sizeof(cs_resp));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(norm_current == cs_resp.get_state.chg_current);

	return EC_SUCCESS;
}

static int test_low_battery_hostevents(void)
{
	int state;

	test_setup(0);

	ccprintf("[CHARGING TEST] Low battery host events\n");

	/* You know you make me wanna */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, BATTERY_LEVEL_LOW + 1);
	ev_clear(EC_HOST_EVENT_BATTERY_LOW);
	ev_clear(EC_HOST_EVENT_BATTERY_CRITICAL);
	ev_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, BATTERY_LEVEL_LOW - 1);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE,
		 CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE + 1);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE,
		 CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE - 1);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, BATTERY_LEVEL_SHUTDOWN + 1);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, BATTERY_LEVEL_SHUTDOWN - 1);
	state = wait_charging_state();
	TEST_ASSERT(state == PWR_STATE_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_CRITICAL));
	/* hey-hey-HEY-hey. Doesn't immediately shut down */
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);
	/* after a while, the AP should shut down */
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
	TEST_ASSERT(is_shutdown);

	return EC_SUCCESS;
}

static int test_battery_sustainer(void)
{
	struct ec_params_charge_control p;
	struct ec_response_charge_control r;
	int rv;

	test_setup(1);

	/* Enable sustainer */
	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_NORMAL;
	p.sustain_soc.lower = 79;
	p.sustain_soc.upper = 80;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				    NULL, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);

	p.cmd = EC_CHARGE_CONTROL_CMD_GET;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p), &r,
				    sizeof(r));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(r.sustain_soc.lower == 79);
	TEST_ASSERT(r.sustain_soc.upper == 80);

	/* Check mode transition as the SoC changes. */

	ccprintf("Test SoC < lower < upper.\n");
	display_soc = 780;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);
	ccprintf("Pass.\n");

	ccprintf("Test lower < upper < SoC.\n");
	display_soc = 810;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);
	ccprintf("Pass.\n");

	ccprintf("Test unplug AC.\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);
	ccprintf("Pass.\n");

	ccprintf("Test replug AC.\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);
	ccprintf("Pass.\n");

	ccprintf("Test lower < SoC < upper.\n");
	display_soc = 799;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);
	ccprintf("Pass.\n");

	ccprintf("Test SoC < lower < upper.\n");
	display_soc = 789;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);
	ccprintf("Pass.\n");

	ccprintf("Test disable sustainer.\n");
	charge_control(CHARGE_CONTROL_NORMAL);
	display_soc = 810;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);
	ccprintf("Pass.\n");

	ccprintf("Test enable sustainer when battery is full.\n");
	display_soc = 1000;
	is_full = 1;
	wait_charging_state();
	/* Enable sustainer. */
	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_NORMAL;
	p.sustain_soc.lower = 79;
	p.sustain_soc.upper = 80;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				    NULL, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);
	ccprintf("Pass.\n");

	/* Disable sustainer, unplug AC, upper < SoC < 100. */
	charge_control(CHARGE_CONTROL_NORMAL);
	display_soc = 810;
	is_full = 0;
	gpio_set_level(GPIO_AC_PRESENT, 0);
	wait_charging_state();

	ccprintf("Test enable sustainer when AC is present.\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	wait_charging_state();
	/* Enable sustainer. */
	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_NORMAL;
	p.sustain_soc.lower = 79;
	p.sustain_soc.upper = 80;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				    NULL, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);
	ccprintf("Pass.\n");

	return EC_SUCCESS;
}

static int test_battery_sustainer_discharge_idle(void)
{
	struct ec_params_charge_control p;
	int rv;

	test_setup(1);

	/* Enable sustainer */
	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_NORMAL;
	p.sustain_soc.lower = 80;
	p.sustain_soc.upper = 80;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				    NULL, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);

	/* Check mode transition as the SoC changes. */

	/* SoC < lower (= upper) */
	display_soc = 780;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);

	/* (lower =) upper < SoC */
	display_soc = 810;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_IDLE);

	/* Unplug AC. Sustainer gets deactivated. */
	gpio_set_level(GPIO_AC_PRESENT, 0);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);

	/* Replug AC. Sustainer gets re-activated. */
	gpio_set_level(GPIO_AC_PRESENT, 1);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_IDLE);

	/* lower = SoC = upper */
	display_soc = 800;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_IDLE);

	/* SoC < lower (= upper) */
	display_soc = 789;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);

	/* Disable sustainer */
	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_NORMAL;
	p.sustain_soc.lower = -1;
	p.sustain_soc.upper = -1;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				    NULL, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);

	/* This time, mode will stay in NORMAL even when upper < SoC. */
	display_soc = 810;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_charge_state);
	RUN_TEST(test_low_battery);
	RUN_TEST(test_high_temp_battery);
	RUN_TEST(test_cold_battery_with_ac);
	RUN_TEST(test_cold_battery_no_ac);
	RUN_TEST(test_external_funcs);
	RUN_TEST(test_hc_charge_state);
	RUN_TEST(test_hc_current_limit);
	RUN_TEST(test_low_battery_hostevents);
	RUN_TEST(test_battery_sustainer);
	RUN_TEST(test_battery_sustainer_discharge_idle);
	RUN_TEST(test_deep_charge_battery);

	test_print_result();
}
