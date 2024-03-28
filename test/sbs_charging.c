/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test charge_state behavior
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

test_static int mock_chipset_state = CHIPSET_STATE_ON;
test_static int is_shutdown;
test_static int is_force_discharge;
test_static int is_hibernated;
test_static int override_voltage, override_current, override_usec;
test_static int display_soc;
test_static int is_full;

/* The simulation doesn't really hibernate, so we must reset this ourselves */
extern timestamp_t shutdown_target_time;
bool battery_sustainer_enabled(void);

test_static void reset_mocks(void)
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

test_static uint32_t meh;
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

test_static int wait_charging_state(void)
{
	enum led_pwr_state state;
	task_wake(TASK_ID_CHARGER);
	msleep(WAIT_CHARGER_TASK);
	state = led_pwr_get_state();
	ccprintf("[CHARGING TEST] state = %d\n", state);
	return state;
}

test_static int charge_control(enum ec_charge_control_mode mode)
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
test_static void test_setup(int on_ac)
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
test_static int ev_is_set(int event)
{
	return host_get_events() & EC_HOST_EVENT_MASK(event);
}
test_static int ev_is_clear(int event)
{
	return !ev_is_set(event);
}
test_static void ev_clear(int event)
{
	host_clear_events(EC_HOST_EVENT_MASK(event));
}

test_static int test_charge_state(void)
{
	enum led_pwr_state state;
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
	TEST_ASSERT(state == LED_PWRS_ERROR);

	/* Attach battery again, charging */
	ccprintf("[CHARGING TEST] Attach battery\n");
	test_attach_i2c(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS);
	/* And changing full capacity should trigger a host event */
	ev_clear(EC_HOST_EVENT_BATTERY);
	sb_write(SB_FULL_CHARGE_CAPACITY, 0xeff0);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY));

	/* Unplug AC, discharging at 1000mAh */
	ccprintf("[CHARGING TEST] AC off\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	flags = charge_get_led_flags();
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_EXTERNAL_POWER));
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_FORCE_IDLE));

	/* Discharging waaaay overtemp is ignored */
	ccprintf("[CHARGING TEST] AC off, batt temp = 0xffff\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	sb_write(SB_TEMPERATURE, 0xffff);
	state = wait_charging_state();
	TEST_ASSERT(!is_shutdown);
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(40));

	/* Discharging overtemp */
	ccprintf("[CHARGING TEST] AC off, batt temp = 90 C\n");
	gpio_set_level(GPIO_AC_PRESENT, 0);
	sb_write(SB_CURRENT, -1000);

	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(90));
	state = wait_charging_state();
	sleep(CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT);
	TEST_ASSERT(is_shutdown);
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	sb_write(SB_TEMPERATURE, CELSIUS_TO_DECI_KELVIN(40));

	/* Force idle */
	ccprintf("[CHARGING TEST] AC on, force idle\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	sb_write(SB_CURRENT, 1000);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);
	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_FORCE_IDLE));
	charge_control(CHARGE_CONTROL_IDLE);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_FORCED_IDLE);
	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(flags & CHARGE_LED_FLAG_FORCE_IDLE);
	charge_control(CHARGE_CONTROL_NORMAL);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);

	/* Force discharge */
	ccprintf("[CHARGING TEST] AC on, force discharge\n");
	gpio_set_level(GPIO_AC_PRESENT, 1);
	sb_write(SB_CURRENT, 1000);
	charge_control(CHARGE_CONTROL_DISCHARGE);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_FORCED_IDLE);
	TEST_ASSERT(is_force_discharge);
	charge_control(CHARGE_CONTROL_NORMAL);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);
	TEST_ASSERT(!is_force_discharge);

	return EC_SUCCESS;
}

test_static int test_low_battery(void)
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

test_static int test_deep_charge_battery(void)
{
	enum charge_state state;
	const struct battery_info *bat_info = battery_get_info();

	test_setup(1);

	/* battery pack voltage bellow voltage_min */
	sb_write(SB_VOLTAGE, (bat_info->voltage_min - 200));
	wait_charging_state();
	state = charge_get_state();
	TEST_ASSERT(state == ST_PRECHARGE);

	/*
	 * Battery voltage keep bellow voltage_min,
	 * precharge over time CONFIG_BATTERY_LOW_VOLTAGE_TIMEOUT
	 */
	usleep(CONFIG_BATTERY_LOW_VOLTAGE_TIMEOUT);
	state = charge_get_state();
	TEST_ASSERT(state == ST_IDLE);

	/* recovery from a low voltage. */
	sb_write(SB_VOLTAGE, (bat_info->voltage_normal));
	wait_charging_state();
	state = charge_get_state();
	TEST_ASSERT(state == ST_CHARGE);

	return EC_SUCCESS;
}
test_static int test_high_temp_battery(void)
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

	ccprintf(
		"[CHARGING TEST] High battery temp && AC off S0->S5 hibernate\n");
	mock_chipset_state = CHIPSET_STATE_SOFT_OFF;
	gpio_set_level(GPIO_AC_PRESENT, 0);
	wait_charging_state();
	TEST_ASSERT(is_hibernated);

	return EC_SUCCESS;
}

test_static int test_cold_battery_with_ac(void)
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

test_static int test_cold_battery_no_ac(void)
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

test_static int test_external_funcs(void)
{
	int rv, temp;
	uint32_t flags;
	int state;

	/* Connect the AC */
	test_setup(1);

	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_FORCE_IDLE));

	/* Invalid or do-nothing commands first */
	UART_INJECT("chg\n");
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);
	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_FORCE_IDLE));

	UART_INJECT("chg blahblah\n");
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);
	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_FORCE_IDLE));

	UART_INJECT("chg idle\n");
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);
	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_FORCE_IDLE));

	UART_INJECT("chg idle blargh\n");
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);
	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_FORCE_IDLE));

	/* Now let's force idle on and off */
	UART_INJECT("chg idle on\n");
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_FORCED_IDLE);
	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(flags & CHARGE_LED_FLAG_FORCE_IDLE);

	UART_INJECT("chg idle off\n");
	wait_charging_state();
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_CHARGE);
	flags = charge_get_led_flags();
	TEST_ASSERT(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	TEST_ASSERT(!(flags & CHARGE_LED_FLAG_FORCE_IDLE));

	/* and the rest */
	TEST_ASSERT(led_pwr_get_state() == LED_PWRS_CHARGE);
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
test_static int test_hc_charge_state(void)
{
	enum led_pwr_state state;
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
		TEST_ASSERT(state == LED_PWRS_CHARGE);

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

test_static int test_hc_current_limit(void)
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

test_static int test_hc_current_limit_v1(void)
{
	int rv, norm_current, lower_current, current;
	struct ec_params_current_limit_v1 params;

	/* On AC */
	test_setup(1);
	display_soc = 700;
	wait_charging_state();

	/* See what current the charger is delivering */
	rv = charger_get_current(0, &norm_current);
	TEST_ASSERT(rv == EC_RES_SUCCESS);

	/* Lower it a bit */
	lower_current = norm_current - 256;
	params.limit = lower_current;
	params.battery_soc = 80;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &params,
				    sizeof(params), 0, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	wait_charging_state();

	/* Check current limit is not applied. */
	rv = charger_get_current(0, &current);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(norm_current == current);

	/* Increase the soc above the slow charge trigger point. */
	display_soc = 900;
	wait_charging_state();

	/* Check current limit is applied. */
	rv = charger_get_current(0, &current);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(current == lower_current);

	/* Remove the limit */
	params.limit = -1U;
	params.battery_soc = 0;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &params,
				    sizeof(params), 0, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	wait_charging_state();

	/* Check current limit is removed. */
	rv = charger_get_current(0, &current);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(norm_current == current);

	/* Test invalid value. */
	params.battery_soc = 101;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &params,
				    sizeof(params), 0, 0);
	TEST_ASSERT(rv = EC_RES_INVALID_PARAM);

	return EC_SUCCESS;
}

test_static int test_low_battery_hostevents(void)
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
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, BATTERY_LEVEL_LOW - 1);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE,
		 CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE + 1);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE,
		 CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE - 1);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, BATTERY_LEVEL_SHUTDOWN + 1);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_LOW));
	TEST_ASSERT(ev_is_set(EC_HOST_EVENT_BATTERY_CRITICAL));
	TEST_ASSERT(ev_is_clear(EC_HOST_EVENT_BATTERY_SHUTDOWN));
	TEST_ASSERT(!is_shutdown);

	/* (Shout) a little bit louder now */
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, BATTERY_LEVEL_SHUTDOWN - 1);
	state = wait_charging_state();
	TEST_ASSERT(state == LED_PWRS_DISCHARGE);
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

test_static int battery_sustainer_set(int version, int8_t lower, int8_t upper,
				      enum ec_charge_control_flag flags)
{
	struct ec_params_charge_control p;

	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_NORMAL;
	p.sustain_soc.lower = lower;
	p.sustain_soc.upper = upper;
	p.flags = flags;
	return test_send_host_command(EC_CMD_CHARGE_CONTROL, version, &p,
				      sizeof(p), NULL, 0);
}

test_static int battery_sustainer_get(int version,
				      struct ec_response_charge_control *r)
{
	struct ec_params_charge_control p;

	p.cmd = EC_CHARGE_CONTROL_CMD_GET;
	return test_send_host_command(EC_CMD_CHARGE_CONTROL, version, &p,
				      sizeof(p), r, sizeof(*r));
}

test_static int test_hc_charge_control_v2(void)
{
	struct ec_response_charge_control r;
	int rv;

	test_setup(1);

	ccprintf("Test v2 command\n");
	rv = battery_sustainer_set(2, 79, 80, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	rv = battery_sustainer_get(2, &r);
	TEST_ASSERT(r.sustain_soc.lower == 79);
	TEST_ASSERT(r.sustain_soc.upper == 80);
	TEST_ASSERT(r.flags == 0);

	ccprintf("Test v2 lower > upper\n");
	rv = battery_sustainer_set(2, 80, 79, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	ccprintf("Test v2 lower < 0\n");
	rv = battery_sustainer_set(2, -100, 80, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	ccprintf("Test v2 100 < upper\n");
	rv = battery_sustainer_set(2, -100, 80, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	return EC_SUCCESS;
}

test_static int test_hc_charge_control_v3(void)
{
	struct ec_params_charge_control p;
	struct ec_response_charge_control r;
	int rv;

	test_setup(1);

	ccprintf("Test v3 command\n");
	rv = battery_sustainer_set(3, 79, 80, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	rv = battery_sustainer_get(3, &r);
	TEST_ASSERT(r.sustain_soc.lower == 79);
	TEST_ASSERT(r.sustain_soc.upper == 80);
	TEST_ASSERT(r.flags == 0);

	ccprintf("Test v3 command with flags\n");
	rv = battery_sustainer_set(3, 79, 80, EC_CHARGE_CONTROL_FLAG_NO_IDLE);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	rv = battery_sustainer_get(3, &r);
	TEST_ASSERT(r.sustain_soc.lower == 79);
	TEST_ASSERT(r.sustain_soc.upper == 80);
	TEST_ASSERT(r.flags == EC_CHARGE_CONTROL_FLAG_NO_IDLE);

	ccprintf("Test v3 lower > upper\n");
	rv = battery_sustainer_set(3, 80, 79, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	ccprintf("Test v3 lower < 0\n");
	rv = battery_sustainer_set(3, -100, 80, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	ccprintf("Test v3 100 < upper\n");
	rv = battery_sustainer_set(3, 79, 101, 0);
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	/* Test illegal command */
	p.cmd = UINT8_MAX;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 3, &p, sizeof(p), &r,
				    sizeof(r));
	TEST_ASSERT(rv == EC_RES_INVALID_PARAM);

	/* Test illegal control mode */
	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_COUNT;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 3, &p, sizeof(p), &r,
				    sizeof(r));
	TEST_ASSERT(rv == EC_RES_ERROR);

	return EC_SUCCESS;
}

test_static int run_battery_sustainer_no_idle(int version)
{
	const enum ec_charge_control_flag flags =
		version > 2 ? EC_CHARGE_CONTROL_FLAG_NO_IDLE : 0;
	int rv;

	test_setup(1);

	/* Enable sustainer */
	rv = battery_sustainer_set(version, 79, 80, flags);
	TEST_ASSERT(rv == EC_RES_SUCCESS);

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
	rv = battery_sustainer_set(version, 79, 80, flags);
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
	rv = battery_sustainer_set(version, 79, 80, flags);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);
	ccprintf("Pass.\n");

	return EC_SUCCESS;
}

test_static int test_battery_sustainer_without_idle(void)
{
	ccprintf("Test v2 without idle\n");
	run_battery_sustainer_no_idle(2);

	ccprintf("Test v3 without idle\n");
	run_battery_sustainer_no_idle(3);

	return EC_SUCCESS;
}

test_static int run_battery_sustainer_with_idle(int version)
{
	int rv;

	test_setup(1);

	/* Enable sustainer */
	if (version > 2)
		rv = battery_sustainer_set(version, 79, 80, 0);
	else
		/* V2 needs lower == upper to enable IDLE. */
		rv = battery_sustainer_set(version, 80, 80, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(battery_sustainer_enabled());

	/* Check mode transition as the SoC changes. */

	/* SoC < lower (= upper) */
	display_soc = 780;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);

	/* (lower =) upper < SoC */
	display_soc = 810;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);

	/* Full */
	display_soc = 1000;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);
	TEST_ASSERT(battery_sustainer_enabled());

	/* Unplug AC. Sustainer gets deactivated. */
	gpio_set_level(GPIO_AC_PRESENT, 0);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);

	/* Replug AC. Sustainer gets re-activated. */
	gpio_set_level(GPIO_AC_PRESENT, 1);
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);

	/* lower = SoC = upper */
	display_soc = 800;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_IDLE);

	/* Emulate restarting with upper smaller than the previous. */
	display_soc = 810;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_DISCHARGE);

	/* SoC < lower (= upper) */
	display_soc = 789;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);

	/* Re-enable sustainer when it's already running */
	rv = battery_sustainer_set(version, 89, 90, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(battery_sustainer_enabled());

	/* Disable sustainer */
	rv = battery_sustainer_set(version, -1, -1, 0);
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(!battery_sustainer_enabled());

	/* This time, mode will stay in NORMAL even when upper < SoC. */
	display_soc = 810;
	wait_charging_state();
	TEST_ASSERT(get_chg_ctrl_mode() == CHARGE_CONTROL_NORMAL);

	return EC_SUCCESS;
}

test_static int test_battery_sustainer_with_idle(void)
{
	ccprintf("Test v2 with idle\n");
	run_battery_sustainer_with_idle(2);

	ccprintf("Test v3 with idle\n");
	run_battery_sustainer_with_idle(3);

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
	RUN_TEST(test_hc_current_limit_v1);
	RUN_TEST(test_hc_charge_control_v2);
	RUN_TEST(test_hc_charge_control_v3);
	RUN_TEST(test_low_battery_hostevents);
	RUN_TEST(test_battery_sustainer_without_idle);
	RUN_TEST(test_battery_sustainer_with_idle);
	RUN_TEST(test_deep_charge_battery);

	test_print_result();
}
