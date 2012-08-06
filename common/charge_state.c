/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging task and state machine.
 */

#include "battery.h"
#include "battery_pack.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "power_button.h"
#include "power_led.h"
#include "printf.h"
#include "smart_battery.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "x86_power.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

/* Voltage debounce time */
#define DEBOUNCE_TIME (10 * SECOND)

/* Time period between setting power LED */
#define SET_LED_PERIOD (10 * SECOND)

static const char * const state_name[] = POWER_STATE_NAME_TABLE;

static int state_machine_force_idle = 0;

/* Current power state context */
static struct power_state_context task_ctx;

static inline int is_charger_expired(
	struct power_state_context *ctx, timestamp_t now)
{
	return now.val - ctx->charger_update_time.val > CHARGER_UPDATE_PERIOD;
}

static inline void update_charger_time(
	struct power_state_context *ctx, timestamp_t now)
{
	ctx->charger_update_time.val = now.val;
}

/* Battery information used to fill ACPI _BIF and/or _BIX */
static void update_battery_info(void)
{
	char *batt_str;
	int batt_serial;

	/* Design Capacity of Full */
	battery_design_capacity((int *)host_get_memmap(EC_MEMMAP_BATT_DCAP));

	/* Design Voltage */
	battery_design_voltage((int *)host_get_memmap(EC_MEMMAP_BATT_DVLT));

	/* Last Full Charge Capacity */
	battery_full_charge_capacity(
		(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC));

	/* Cycle Count */
	battery_cycle_count((int *)host_get_memmap(EC_MEMMAP_BATT_CCNT));

	/* Battery Manufacturer string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_MFGR);
	memset(batt_str, 0, EC_MEMMAP_TEXT_MAX);
	battery_manufacturer_name(batt_str, EC_MEMMAP_TEXT_MAX);

	/* Battery Model string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_MODEL);
	memset(batt_str, 0, EC_MEMMAP_TEXT_MAX);
	battery_device_name(batt_str, EC_MEMMAP_TEXT_MAX);

	/* Battery Type string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_TYPE);
	battery_device_chemistry(batt_str, EC_MEMMAP_TEXT_MAX);

	/* Smart battery serial number is 16 bits */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_SERIAL);
	memset(batt_str, 0, EC_MEMMAP_TEXT_MAX);
	if (battery_serial_number(&batt_serial) == 0)
		snprintf(batt_str, EC_MEMMAP_TEXT_MAX, "%04X", batt_serial);

	/* Battery data is now present */
	*host_get_memmap(EC_MEMMAP_BATTERY_VERSION) = 1;
}

/* Prevent battery from going into deep discharge state */
static void poweroff_wait_ac(void)
{
	/* Shutdown the main processor */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* chipset_force_state(CHIPSET_STATE_SOFT_OFF);
		 * TODO(rong): remove platform dependent code
		 */
#ifdef CONFIG_TASK_X86POWER
		x86_power_force_shutdown();
#endif /* CONFIG_TASK_X86POWER */
	}
}

/* Common handler for charging states.
 * This handler gets battery charging parameters, charger state, ac state,
 * and timestamp. It also fills memory map and issues power events on state
 * change.
 */
static int state_common(struct power_state_context *ctx)
{
	int rv, d;

	struct power_state_data *curr = &ctx->curr;
	struct power_state_data *prev = &ctx->prev;
	struct batt_params *batt = &ctx->curr.batt;
	uint8_t *batt_flags = ctx->memmap_batt_flags;

	/* Copy previous state and init new state */
	ctx->prev = ctx->curr;
	curr->ts = get_time();
	curr->error = 0;

	/* Detect AC change */
	curr->ac = power_ac_present();
	if (curr->ac != prev->ac) {
		if (curr->ac) {
			/* AC on
			 *   Initialize charger to power on reset mode
			 */
			rv = charger_post_init();
			if (rv)
				curr->error |= F_CHARGER_INIT;
			host_set_single_event(EC_HOST_EVENT_AC_CONNECTED);
		} else {
			/* AC off */
			host_set_single_event(EC_HOST_EVENT_AC_DISCONNECTED);
		}
	}

	if (curr->ac) {
		*batt_flags |= EC_BATT_FLAG_AC_PRESENT;
		rv = charger_get_voltage(&curr->charging_voltage);
		if (rv) {
			charger_set_voltage(0);
			charger_set_current(0);
			curr->error |= F_CHARGER_VOLTAGE;
		}
		rv = charger_get_current(&curr->charging_current);
		if (rv) {
			charger_set_voltage(0);
			charger_set_current(0);
			curr->error |= F_CHARGER_CURRENT;
		}
	} else {
		*batt_flags &= ~EC_BATT_FLAG_AC_PRESENT;
		/* AC disconnected should get us out of force idle mode. */
		state_machine_force_idle = 0;
	}

	rv = battery_temperature(&batt->temperature);
	if (rv) {
		/* Check low battery condition and retry */
		if (curr->ac && !(curr->error & F_CHARGER_MASK) &&
				(curr->charging_voltage == 0 ||
				curr->charging_current == 0)) {
			charger_set_voltage(ctx->battery->voltage_min);
			charger_set_current(ctx->charger->current_min);
			usleep(SECOND);
			rv = battery_temperature(&batt->temperature);
		}
	}

	if (rv)
		curr->error |= F_BATTERY_TEMPERATURE;

	rv = battery_voltage(&batt->voltage);
	if (rv)
		curr->error |= F_BATTERY_VOLTAGE;
	*ctx->memmap_batt_volt = batt->voltage;

	rv = battery_current(&batt->current);
	if (rv)
		curr->error |= F_BATTERY_CURRENT;
	/* Memory mapped value: discharge rate */
	*ctx->memmap_batt_rate = batt->current < 0 ?
		-batt->current : batt->current;

	rv = battery_desired_voltage(&batt->desired_voltage);
	if (rv)
		curr->error |= F_DESIRED_VOLTAGE;

	rv = battery_desired_current(&batt->desired_current);
	if (rv)
		curr->error |= F_DESIRED_CURRENT;

	rv = battery_state_of_charge(&batt->state_of_charge);
	if (rv)
		curr->error |= F_BATTERY_STATE_OF_CHARGE;

	if (batt->state_of_charge != prev->batt.state_of_charge) {
		rv = battery_full_charge_capacity(&d);
		if (!rv && d != *(int*)host_get_memmap(EC_MEMMAP_BATT_LFCC)) {
			*(int*)host_get_memmap(EC_MEMMAP_BATT_LFCC) = d;
			/* Notify host to re-read battery information */
			host_set_single_event(EC_HOST_EVENT_BATTERY);
		}
	}

	/* Prevent deep discharging */
	if (!curr->ac)
		if ((batt->state_of_charge < BATTERY_LEVEL_SHUTDOWN &&
		    !(curr->error & F_BATTERY_STATE_OF_CHARGE)) ||
		    (batt->voltage <= ctx->battery->voltage_min &&
		    !(curr->error & F_BATTERY_VOLTAGE)))
			poweroff_wait_ac();

	/* Check battery presence */
	if (curr->error & F_BATTERY_MASK) {
		*ctx->memmap_batt_flags &= ~EC_BATT_FLAG_BATT_PRESENT;
		return curr->error;
	}

	*ctx->memmap_batt_flags |= EC_BATT_FLAG_BATT_PRESENT;

	/* Battery charge level low */
	if (batt->state_of_charge <= BATTERY_LEVEL_LOW &&
			prev->batt.state_of_charge > BATTERY_LEVEL_LOW)
		host_set_single_event(EC_HOST_EVENT_BATTERY_LOW);

	/* Battery charge level critical */
	if (batt->state_of_charge <= BATTERY_LEVEL_CRITICAL) {
		*ctx->memmap_batt_flags |= EC_BATT_FLAG_LEVEL_CRITICAL;
		/* Send battery critical host event */
		if (prev->batt.state_of_charge > BATTERY_LEVEL_CRITICAL)
			host_set_single_event(EC_HOST_EVENT_BATTERY_CRITICAL);
	} else
		*ctx->memmap_batt_flags &= ~EC_BATT_FLAG_LEVEL_CRITICAL;


	/* Apply battery pack vendor charging method */
	battery_vendor_params(batt);

#ifdef CONFIG_CHARGING_CURRENT_LIMIT
	if (batt->desired_current > CONFIG_CHARGING_CURRENT_LIMIT)
		batt->desired_current = CONFIG_CHARGING_CURRENT_LIMIT;
#endif

	rv = battery_get_battery_mode(&d);
	if (rv) {
		curr->error |= F_BATTERY_MODE;
	} else {
		if (d & MODE_CAPACITY) {
			/* Battery capacity mode was set to mW
			 * reset it back to mAh
			 */
			d &= ~MODE_CAPACITY;
			rv = battery_set_battery_mode(d);
			if (rv)
				ctx->curr.error |= F_BATTERY_MODE;
		}
	}
	rv = battery_remaining_capacity(&d);
	if (rv)
		ctx->curr.error |= F_BATTERY_CAPACITY;
	else
		*ctx->memmap_batt_cap = d;

	return ctx->curr.error;
}

/* Init state handler
 *	- check ac, charger, battery and temperature
 *	- initialize charger
 *	- new states: DISCHARGE, IDLE
 */
static enum power_state state_init(struct power_state_context *ctx)
{
	/* Stop charger, unconditionally */
	charger_set_current(0);
	charger_set_voltage(0);

	/* If AC is not present, switch to discharging state */
	if (!ctx->curr.ac)
		return PWR_STATE_DISCHARGE;

	/* Check general error conditions */
	if (ctx->curr.error)
		return PWR_STATE_ERROR;

	/* Update static battery info */
	update_battery_info();

	/* Send battery event to host */
	host_set_single_event(EC_HOST_EVENT_BATTERY);

	return PWR_STATE_IDLE;
}

/* Idle state handler
 *	- both charger and battery are online
 *	- detect charger and battery status change
 *	- new states: CHARGE, INIT
 */
static enum power_state state_idle(struct power_state_context *ctx)
{
	struct batt_params *batt = &ctx->curr.batt;
	const struct charger_info *c_info = ctx->charger;

	/* If we are forcing idle mode, then just stay in IDLE. */
	if (state_machine_force_idle)
		return PWR_STATE_UNCHANGE;

	if (!ctx->curr.ac)
		return PWR_STATE_INIT;

	if (ctx->curr.error)
		return PWR_STATE_ERROR;

	/* Prevent charging in idle mode */
	if (ctx->curr.charging_voltage ||
	    ctx->curr.charging_current)
		return PWR_STATE_INIT;

	if (ctx->curr.batt.state_of_charge >= STOP_CHARGE_THRESHOLD)
		return PWR_STATE_UNCHANGE;

	/* Configure init charger state and switch to charge state */
	if (ctx->curr.batt.desired_voltage &&
	    ctx->curr.batt.desired_current) {
		/* Set charger output constraints */
		if (batt->desired_current < ctx->charger->current_min) {
			/* Trickle charging */
			if (charger_set_current(c_info->current_min) ||
			    charger_set_voltage(batt->voltage))
				return PWR_STATE_ERROR;
			ctx->trickle_charging_time = get_time();
		} else {
			/* Normal charging */
			if (charger_set_voltage(batt->desired_voltage) ||
			    charger_set_current(batt->desired_current))
				return PWR_STATE_ERROR;
		}
		update_charger_time(ctx, get_time());
		return PWR_STATE_CHARGE;
	}

	return PWR_STATE_UNCHANGE;
}

/* Charge state handler
 *	- detect battery status change
 *	- new state: INIT
 */
static enum power_state state_charge(struct power_state_context *ctx)
{
	struct power_state_data *curr = &ctx->curr;
	struct batt_params *batt = &ctx->curr.batt;
	const struct charger_info *c_info = ctx->charger;
	int debounce = 0;
	timestamp_t now;

	if (curr->error)
		return PWR_STATE_ERROR;

	if (batt->desired_current < c_info->current_min &&
	    batt->desired_current > 0)
		return trickle_charge(ctx);

	/* Check charger reset */
	if (curr->charging_voltage == 0 ||
	    curr->charging_current == 0)
		return PWR_STATE_INIT;

	if (!curr->ac)
		return PWR_STATE_INIT;

	if (batt->state_of_charge >= STOP_CHARGE_THRESHOLD) {
		if (charger_set_voltage(0) || charger_set_current(0))
			return PWR_STATE_ERROR;
		return PWR_STATE_IDLE;
	}

	now = get_time();

	if (batt->desired_voltage != curr->charging_voltage) {
		if (charger_set_voltage(batt->desired_voltage))
			return PWR_STATE_ERROR;
		update_charger_time(ctx, now);
	}

	if (batt->desired_current == curr->charging_current) {
		/* Tick charger watchdog */
		if (!is_charger_expired(ctx, now))
			return PWR_STATE_UNCHANGE;
	} else if (batt->desired_current > curr->charging_current) {
		if (!timestamp_expired(ctx->voltage_debounce_time, &now))
			return PWR_STATE_UNCHANGE;
	} else {
		/* Debounce charging current on falling edge */
		debounce = 1;
	}

	if (charger_set_current(batt->desired_current))
		return PWR_STATE_ERROR;

	/* Update charger watchdog timer and debounce timer */
	update_charger_time(ctx, now);
	if (debounce)
		ctx->voltage_debounce_time.val = now.val + DEBOUNCE_TIME;

	return PWR_STATE_UNCHANGE;
}

/* Discharge state handler
 *	- detect ac status
 *	- new state: INIT
 */
static enum power_state state_discharge(struct power_state_context *ctx)
{
	struct batt_params *batt = &ctx->curr.batt;
	if (ctx->curr.ac)
		return PWR_STATE_INIT;

	if (ctx->curr.error)
		return PWR_STATE_ERROR;

	/* Overtemp in discharging state
	 *   - poweroff host and ec
	 */
	if (batt->temperature > ctx->battery->temp_discharge_max ||
	    batt->temperature < ctx->battery->temp_discharge_min)
		poweroff_wait_ac();

	return PWR_STATE_UNCHANGE;
}

/* Error state handler
 *	- check charger and battery communication
 *	- log error
 *	- new state: INIT
 */
static enum power_state state_error(struct power_state_context *ctx)
{
	static int logged_error;

	if (!ctx->curr.error) {
		logged_error = 0;
		return PWR_STATE_INIT;
	}

	/* Debug output */
	if (ctx->curr.error != logged_error) {
		CPRINTF("[Charge error: flag[%08b -> %08b], ac %d, "
			" charger %s, battery %s\n",
			logged_error, ctx->curr.error, ctx->curr.ac,
			(ctx->curr.error & F_CHARGER_MASK) ?
					"(err)" : "ok",
			(ctx->curr.error & F_BATTERY_MASK) ?
					"(err)" : "ok");

		logged_error = ctx->curr.error;
	}

	return PWR_STATE_UNCHANGE;
}

static void charging_progress(struct power_state_context *ctx)
{
	int seconds, minutes;

	if (ctx->curr.batt.state_of_charge !=
	    ctx->prev.batt.state_of_charge) {
		if (ctx->curr.ac)
			battery_time_to_full(&minutes);
		else
			battery_time_to_empty(&minutes);

		CPRINTF("[Battery %3d%% / %dh:%d]\n",
			ctx->curr.batt.state_of_charge,
			minutes / 60, minutes % 60);
		return;
	}

	if (ctx->curr.charging_voltage != ctx->prev.charging_voltage &&
			ctx->trickle_charging_time.val) {
		/* Calculating minutes by dividing usec by 60 million
		 * GNU toolchain generate architecture dependent calls
		 * instead of machine code when the divisor is large.
		 * Hence following calculation was broke into 2 lines.
		 */
		seconds = (int)(get_time().val -
				ctx->trickle_charging_time.val) / (int)SECOND;
		minutes = seconds / 60;
		CPRINTF("[Precharge CHG(%dmV) BATT(%dmV %dmA) "
			"%dh:%d]\n", ctx->curr.charging_voltage,
			ctx->curr.batt.voltage, ctx->curr.batt.current,
			minutes / 60, minutes % 60);
	}
}

enum power_state charge_get_state(void)
{
	return task_ctx.curr.state;
}

int charge_get_percent(void)
{
	return task_ctx.curr.batt.state_of_charge;
}

static int enter_force_idle_mode(void)
{
	if (!power_ac_present())
		return EC_ERROR_UNKNOWN;
	state_machine_force_idle = 1;
	charger_post_init();
	return EC_SUCCESS;
}

static int exit_force_idle_mode(void)
{
	state_machine_force_idle = 0;
	return EC_SUCCESS;
}

static enum powerled_color force_idle_led_blink(void)
{
	static enum powerled_color last = POWERLED_GREEN;
	if (last == POWERLED_GREEN)
		last = POWERLED_OFF;
	else
		last = POWERLED_GREEN;
	return last;
}

/* Battery charging task */
void charge_state_machine_task(void)
{
	struct power_state_context *ctx = &task_ctx;
	timestamp_t ts;
	int sleep_usec = POLL_PERIOD_SHORT, diff_usec, sleep_next;
	enum power_state new_state;
	uint8_t batt_flags;
	enum powerled_color led_color = POWERLED_OFF;
	int rv_setled = 0;
	uint64_t last_setled_time = 0;

	ctx->prev.state = PWR_STATE_INIT;
	ctx->curr.state = PWR_STATE_INIT;
	ctx->trickle_charging_time.val = 0;
	ctx->battery = battery_get_info();
	ctx->charger = charger_get_info();

	/* Setup LPC direct memmap */
	ctx->memmap_batt_volt =
		(uint32_t *)host_get_memmap(EC_MEMMAP_BATT_VOLT);
	ctx->memmap_batt_rate =
		(uint32_t *)host_get_memmap(EC_MEMMAP_BATT_RATE);
	ctx->memmap_batt_cap =
		(uint32_t *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	ctx->memmap_batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);

	while (1) {

		state_common(ctx);

		switch (ctx->prev.state) {
		case PWR_STATE_INIT:
			new_state = state_init(ctx);
			break;
		case PWR_STATE_IDLE:
			new_state = state_idle(ctx);
			break;
		case PWR_STATE_DISCHARGE:
			new_state = state_discharge(ctx);
			break;
		case PWR_STATE_CHARGE:
			new_state = state_charge(ctx);
			break;
		case PWR_STATE_ERROR:
			new_state = state_error(ctx);
			break;
		default:
			CPRINTF("[Undefined charging state %d]\n",
				ctx->curr.state);
			ctx->curr.state = PWR_STATE_ERROR;
			new_state = PWR_STATE_ERROR;
		}

		if (state_machine_force_idle &&
		    ctx->prev.state != PWR_STATE_IDLE &&
		    ctx->prev.state != PWR_STATE_INIT)
			new_state = PWR_STATE_INIT;

		if (new_state) {
			ctx->curr.state = new_state;
			CPRINTF("[Charge state %s -> %s]\n",
				state_name[ctx->prev.state],
				state_name[new_state]);
		}


		switch (new_state) {
		case PWR_STATE_IDLE:
			batt_flags = *ctx->memmap_batt_flags;
			batt_flags &= ~EC_BATT_FLAG_CHARGING;
			batt_flags &= ~EC_BATT_FLAG_DISCHARGING;
			*ctx->memmap_batt_flags = batt_flags;

			/* Charge done */
			led_color = POWERLED_GREEN;
			rv_setled = powerled_set(POWERLED_GREEN);
			last_setled_time = get_time().val;

			sleep_usec = POLL_PERIOD_LONG;
			break;
		case PWR_STATE_DISCHARGE:
			batt_flags = *ctx->memmap_batt_flags;
			batt_flags &= ~EC_BATT_FLAG_CHARGING;
			batt_flags |= EC_BATT_FLAG_DISCHARGING;
			*ctx->memmap_batt_flags = batt_flags;
			sleep_usec = POLL_PERIOD_LONG;
			break;
		case PWR_STATE_CHARGE:
			batt_flags = *ctx->memmap_batt_flags;
			batt_flags |= EC_BATT_FLAG_CHARGING;
			batt_flags &= ~EC_BATT_FLAG_DISCHARGING;
			*ctx->memmap_batt_flags = batt_flags;

			/* Charging */
			led_color = POWERLED_YELLOW;
			rv_setled = powerled_set(POWERLED_YELLOW);
			last_setled_time = get_time().val;

			sleep_usec = POLL_PERIOD_CHARGE;
			break;
		case PWR_STATE_ERROR:
			/* Error */
			led_color = POWERLED_RED;
			rv_setled = powerled_set(POWERLED_RED);
			last_setled_time = get_time().val;

			sleep_usec = POLL_PERIOD_CHARGE;
			break;
		case PWR_STATE_UNCHANGE:
			/* Don't change sleep duration */
			if (state_machine_force_idle)
				powerled_set(force_idle_led_blink());
			else if (rv_setled || get_time().val - last_setled_time
					> SET_LED_PERIOD) {
				/*
				 * It is possible to make power LED go off
				 * without disconnecting AC. Therefore we
				 * need to reset power LED periodically.
				 */
				rv_setled = powerled_set(led_color);
				last_setled_time = get_time().val;
			}
			break;
		default:
			/* Other state; poll quickly and hope it goes away */
			sleep_usec = POLL_PERIOD_SHORT;
		}

		/* Show charging progress in console */
		charging_progress(ctx);

		ts = get_time();
		diff_usec = (int)(ts.val - ctx->curr.ts.val);
		sleep_next = sleep_usec - diff_usec;

		if (sleep_next < MIN_SLEEP_USEC)
			sleep_next = MIN_SLEEP_USEC;
		if (sleep_next > MAX_SLEEP_USEC)
			sleep_next = MAX_SLEEP_USEC;

		usleep(sleep_next);
	}
}

static int charge_command_force_idle(struct host_cmd_handler_args *args)
{
	const struct ec_params_force_idle *p = args->params;
	int rv;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (p->enabled)
		rv = enter_force_idle_mode();
	else
		rv = exit_force_idle_mode();

	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_FORCE_IDLE, charge_command_force_idle,
		     EC_VER_MASK(0));
