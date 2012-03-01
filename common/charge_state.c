/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging task and state machine.
 */


#include "battery_pack.h"
#include "board.h"
#include "console.h"
#include "charger.h"
#include "gpio.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "smart_battery.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Stop charge when state of charge reaches this percentage */
#define STOP_CHARGE_THRESHOLD 100
/* Critical level of when discharging reaches this percentage */
#define BATT_CRITICAL_LEVEL   10

/* power state task polling period in usec */
#define POLL_PERIOD_LONG        500000
#define POLL_PERIOD_CHARGE      250000
#define POLL_PERIOD_SHORT       100000
#define MIN_SLEEP_USEC          50000

/* Power states */
enum power_state {
	PWR_STATE_UNCHANGE = 0,
	PWR_STATE_INIT,
	PWR_STATE_IDLE,
	PWR_STATE_DISCHARGE,
	PWR_STATE_CHARGE,
	PWR_STATE_ERROR
};
/* Debugging constants, in the same order as power_state.
 * This state name table and debug print will be removed
 * before production.
 */
const static char *_state_name[] = {
	"null",
	"init",
	"idle",
	"discharge",
	"charge",
	"error"
};


/* helper function(s) */
static inline int get_ac(void)
{
	return gpio_get_level(GPIO_AC_PRESENT);
}

/* Get battery charging parameters from battery and
 * battery pack vendor table
 */
static int battery_params(struct batt_params *batt)
{
	int rv, d;
	/* Direct mem mapped EC data */
	uint32_t *memmap_batt_volt  = (uint32_t*)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_VOLT);
	uint32_t *memmap_batt_rate  = (uint32_t*)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_RATE);
	uint32_t *memmap_batt_cap   = (uint32_t*)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_CAP);

	rv = battery_temperature(&batt->temperature);
	if (rv)
		return rv;

	rv = battery_voltage(&batt->voltage);
	if (rv)
		return rv;
	*memmap_batt_volt = batt->voltage;

	rv = battery_current(&batt->current);
	if (rv)
		return rv;
	*memmap_batt_rate = batt->current < 0 ? -batt->current : 0;

	rv = battery_desired_voltage(&batt->desired_voltage);
	if (rv)
		return rv;

	rv = battery_desired_current(&batt->desired_current);
	if (rv)
		return rv;

	battery_vendor_params(batt);

	rv = battery_get_battery_mode(&d);
	if (rv)
		return rv;

	if (d & MODE_CAPACITY) {
		/* Battery capacity mode was set to mW, set it back to mAh */
		d &= ~MODE_CAPACITY;
		rv = battery_set_battery_mode(d);
		if (rv)
			return rv;
	}
	rv = battery_remaining_capacity(&d);
	if (rv)
		return rv;
	*memmap_batt_cap = d;

	return EC_SUCCESS;
}

/* Init state handler
 *	- check ac, charger, battery and temperature
 *	- initialize charger
 *	- new states: DISCHARGE, IDLE
 */
static enum power_state state_init(void)
{
	int rv, val;

	/* Stop charger, unconditionally */
	charger_set_current(0);
	charger_set_voltage(0);

	/* Detect AC, init charger */
	if (!get_ac())
		return PWR_STATE_DISCHARGE;

	/* Initialize charger to power on reset mode */
	rv = charger_post_init();
	if (rv)
		return PWR_STATE_ERROR;
	/* Check if charger is online */
	rv = charger_get_status(&val);
	if (rv)
		return PWR_STATE_ERROR;

	/* Detect battery */
	if (battery_temperature(&val))
		return PWR_STATE_ERROR;

	return PWR_STATE_IDLE;
}

/* Idle state handler
 *	- both charger and battery are online
 *	- detect charger and battery status change
 *	- new states: CHARGE, INIT
 */
static enum power_state state_idle(void)
{
	int voltage, current, state_of_charge;
	struct batt_params batt;

	if (!get_ac())
		return PWR_STATE_INIT;

	/* Prevent charging in idle mode */
	if (charger_get_voltage(&voltage))
		return PWR_STATE_ERROR;
	if (charger_get_current(&current))
		return PWR_STATE_ERROR;
	if (voltage || current)
		return PWR_STATE_INIT;

	if (battery_state_of_charge(&state_of_charge))
		return PWR_STATE_ERROR;

	if (state_of_charge >= STOP_CHARGE_THRESHOLD)
		return PWR_STATE_UNCHANGE;

	/* Check if the batter is good to charge */
	if (battery_params(&batt))
		return PWR_STATE_ERROR;

	/* Configure init charger state and switch to charge state */
	if (batt.desired_voltage && batt.desired_current) {
		if (charger_set_voltage(batt.desired_voltage))
			return PWR_STATE_ERROR;
		if (charger_set_current(batt.desired_current))
			return PWR_STATE_ERROR;
		return PWR_STATE_CHARGE;
	}

	return PWR_STATE_UNCHANGE;
}

/* Charge state handler
 *	- detect battery status change
 *	- new state: INIT
 */
static enum power_state state_charge(void)
{
	int chg_voltage, chg_current;
	struct batt_params batt;

	if (!get_ac())
		return PWR_STATE_INIT;

	if (charger_get_voltage(&chg_voltage))
		return PWR_STATE_ERROR;

	if (charger_get_current(&chg_current))
		return PWR_STATE_ERROR;

	/* Check charger reset */
	if (chg_voltage == 0 || chg_current == 0)
		return PWR_STATE_INIT;

	if (battery_params(&batt))
		return PWR_STATE_ERROR;

	if (batt.desired_voltage != chg_voltage)
		if (charger_set_voltage(batt.desired_voltage))
			return PWR_STATE_ERROR;
	if (batt.desired_current != chg_current)
		if (charger_set_current(batt.desired_current))
			return PWR_STATE_ERROR;

	if (battery_state_of_charge(&batt.state_of_charge))
		return PWR_STATE_ERROR;

	if (batt.state_of_charge >= STOP_CHARGE_THRESHOLD) {
		if (charger_set_voltage(0) || charger_set_current(0))
			return PWR_STATE_ERROR;
		return PWR_STATE_IDLE;
	}

	return PWR_STATE_UNCHANGE;
}

/* Discharge state handler
 *	- detect ac status
 *	- new state: INIT
 */
static enum power_state state_discharge(void)
{
	struct batt_params batt;
	uint8_t *memmap_batt_flags = (uint8_t*)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_FLAG);
	if (get_ac())
		return PWR_STATE_INIT;

	if (battery_params(&batt))
		return PWR_STATE_ERROR;

	if (batt.state_of_charge <= BATT_CRITICAL_LEVEL)
		*memmap_batt_flags |= EC_BATT_FLAG_LEVEL_CRITICAL;

	/* TODO: handle overtemp in discharge mode */

	return PWR_STATE_UNCHANGE;
}

/* Error state handler
 *	- check charger and battery communication
 *	- log error
 *	- new state: INIT
 */
static enum power_state state_error(void)
{
	enum { F_CHG_V, F_CHG_I, F_BAT_V, F_BAT_I,
		F_DES_V, F_DES_I, F_BAT_T, F_LAST };
	static int last_error_flags;
	int error_flags = 0;
	int ac = 0;
	int bat_v = -1, bat_i = -1, bat_temp = -1;
	int desired_v = -1, desired_i = -1;
	uint8_t batt_flags = 0;
	uint8_t *memmap_batt_flags = (uint8_t*)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_FLAG);

	ac = get_ac();
	if (ac) {
		batt_flags = EC_BATT_FLAG_AC_PRESENT;
		if (charger_set_voltage(0))
			error_flags |= (1 << F_CHG_V);
		if (charger_set_current(0))
			error_flags |= (1 << F_CHG_I);
	} else
		batt_flags = EC_BATT_FLAG_DISCHARGING;

	if (battery_voltage(&bat_v))
		error_flags |= (1 << F_BAT_V);
	if (battery_current(&bat_i))
		error_flags |= (1 << F_BAT_I);
	if (battery_temperature(&bat_temp))
		error_flags |= (1 << F_BAT_T);
	if (battery_desired_voltage(&desired_v))
		error_flags |= (1 << F_DES_V);
	if (battery_desired_current(&desired_i))
		error_flags |= (1 << F_DES_I);

	if (error_flags == 0) {
		last_error_flags = 0;
		return PWR_STATE_INIT;
	}

	/* Check if all battery operation returned success */
	if (!(error_flags & (F_BAT_V | F_BAT_I | F_DES_V | F_DES_I | F_BAT_T)))
		batt_flags |= EC_BATT_FLAG_BATT_PRESENT;

	/* Debug output */
	if (error_flags != last_error_flags) {
		uart_printf("errors   : %02x\n", error_flags);
		uart_printf("previous : %02x\n", last_error_flags);
		last_error_flags = error_flags;
		uart_printf("ac       : %d\n", ac);
		uart_puts("charger\n");
		if (ac)
			if (error_flags & (F_CHG_V | F_CHG_I))
				uart_puts("  error\n");
			else
				uart_puts("  ok\n");
		else
			uart_puts("  offline\n");

		uart_puts("battery\n");
		uart_printf("  voltage: %5d\n", bat_v);
		uart_printf("  current: %5d\n", bat_i);
		uart_printf("  temp   : %5d\n", (bat_temp - 2731) / 10);
		uart_printf("  des_vol: %5d\n", desired_v);
		uart_printf("  des_cur: %5d\n", desired_i);
	}

	*memmap_batt_flags = batt_flags;

	return PWR_STATE_UNCHANGE;
}

static void charging_progress(void)
{
	static int state_of_charge;
	int d;

	if (battery_state_of_charge(&d))
		return;

	if (d != state_of_charge) {
		state_of_charge = d;
		if (get_ac())
			battery_time_to_full(&d);
		else
			battery_time_to_empty(&d);

		uart_printf("[Battery %3d%% / %dh:%d]\n", state_of_charge,
			d / 60, d % 60);
	}

}

/* Battery charging task */
void charge_state_machine_task(void)
{
	timestamp_t prev_ts, ts;
	int sleep_usec, diff_usec;
	enum power_state current_state, new_state;
	uint8_t *memmap_batt_flags = (uint8_t*)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_FLAG);
	uint8_t batt_flags;

	prev_ts.val = 0;
	current_state = PWR_STATE_INIT;

	while (1) {
		ts = get_time();

		switch (current_state) {
		case PWR_STATE_INIT:
			new_state = state_init();
			break;
		case PWR_STATE_IDLE:
			new_state = state_idle();
			break;
		case PWR_STATE_DISCHARGE:
			new_state = state_discharge();
			break;
		case PWR_STATE_CHARGE:
			new_state = state_charge();
			break;
		case PWR_STATE_ERROR:
			new_state = state_error();
			break;
		default:
			new_state = PWR_STATE_ERROR;
		}

		if (new_state)
			uart_printf("CHARGE: %s --> %s\n",
				_state_name[current_state],
				_state_name[new_state]);

		switch (new_state) {
		case PWR_STATE_IDLE:
			*memmap_batt_flags = EC_BATT_FLAG_AC_PRESENT |
				EC_BATT_FLAG_BATT_PRESENT;
			sleep_usec = POLL_PERIOD_LONG;
			break;
		case PWR_STATE_DISCHARGE:
			batt_flags = *memmap_batt_flags;
			*memmap_batt_flags = EC_BATT_FLAG_AC_PRESENT |
				EC_BATT_FLAG_BATT_PRESENT |
				EC_BATT_FLAG_DISCHARGING |
				(batt_flags & EC_BATT_FLAG_LEVEL_CRITICAL);
			sleep_usec = POLL_PERIOD_LONG;
			break;
		case PWR_STATE_CHARGE:
			*memmap_batt_flags = EC_BATT_FLAG_AC_PRESENT |
				EC_BATT_FLAG_BATT_PRESENT |
				EC_BATT_FLAG_CHARGING;
			sleep_usec = POLL_PERIOD_CHARGE;
			break;
		case PWR_STATE_ERROR:
			sleep_usec = POLL_PERIOD_SHORT;
			break;
		default:
			sleep_usec = POLL_PERIOD_SHORT;
		}

		diff_usec = (int)(ts.val - prev_ts.val);
		sleep_usec -= diff_usec;
		if (sleep_usec < MIN_SLEEP_USEC)
			sleep_usec = MIN_SLEEP_USEC;

		prev_ts = ts;
		usleep(sleep_usec);

		if (new_state)
			current_state = new_state;

		charging_progress();
	}
}

