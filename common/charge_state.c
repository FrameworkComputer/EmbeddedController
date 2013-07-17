/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging task and state machine.
 */

#include "battery.h"
#include "battery_pack.h"
#include "board.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "smart_battery.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

/* Voltage debounce time */
#define DEBOUNCE_TIME (10 * SECOND)

/* Timeout after AP battery shutdown warning before we kill the AP */
#define LOW_BATTERY_SHUTDOWN_TIMEOUT_US (30 * SECOND)

static const char * const state_name[] = POWER_STATE_NAME_TABLE;

static int state_machine_force_idle = 0;

static unsigned user_current_limit = -1U;

static int fake_state_of_charge = -1;

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

/**
 * Update memory-mapped battery information, used by ACPI _BIF and/or _BIX.
 */
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

/**
 * Prevent battery from going into deep discharge state
 */
static void low_battery_shutdown(struct power_state_context *ctx)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		/* AP is off, so shut down the EC now */
		CPRINTF("[%T charge force EC hibernate due to low battery]\n");
		system_hibernate(0, 0);
	} else if (!ctx->shutdown_warning_time.val) {
		/* Warn AP battery level is so low we'll shut down */
		CPRINTF("[%T charge warn shutdown due to low battery]\n");
		ctx->shutdown_warning_time = get_time();
		host_set_single_event(EC_HOST_EVENT_BATTERY_SHUTDOWN);
	} else if (get_time().val > ctx->shutdown_warning_time.val +
		   LOW_BATTERY_SHUTDOWN_TIMEOUT_US) {
		/* Timeout waiting for AP to shut down, so kill it */
		CPRINTF("[%T charge force shutdown due to low battery]\n");
		chipset_force_shutdown();
	}
}

/**
 * Common handler for charging states.
 *
 * This handler gets battery charging parameters, charger state, ac state, and
 * timestamp. It also fills memory map and issues power events on state change.
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
	curr->ac = charge_get_flags() & CHARGE_FLAG_EXTERNAL_POWER;
	if (curr->ac != prev->ac) {
		if (curr->ac) {
			/* AC on
			 *   Initialize charger to power on reset mode
			 */
			rv = charger_post_init();
			if (rv)
				curr->error |= F_CHARGER_INIT;
		}
	}

	if (curr->ac) {
		*batt_flags |= EC_BATT_FLAG_AC_PRESENT;
		if (charger_get_voltage(&curr->charging_voltage)) {
			charger_set_voltage(0);
			charger_set_current(0);
			curr->error |= F_CHARGER_VOLTAGE;
		}
		if (charger_get_current(&curr->charging_current)) {
			charger_set_voltage(0);
			charger_set_current(0);
			curr->error |= F_CHARGER_CURRENT;
		}
	} else {
		*batt_flags &= ~EC_BATT_FLAG_AC_PRESENT;
		/* AC disconnected should get us out of force idle mode. */
		state_machine_force_idle = 0;
	}

#ifdef CONFIG_BATTERY_CHECK_CONNECTED
	if (!battery_is_connected()) {
		curr->error |= F_BATTERY_NOT_CONNECTED;
		return curr->error;
	}
#endif /* CONFIG_BATTERY_CHECK_CONNECTED */

	/* Read temperature and see if battery is responsive */
	rv = battery_temperature(&batt->temperature);
	if (rv) {
		/* Check low battery condition and retry */
		if (curr->ac && ctx->battery_responsive &&
		    !(curr->error & F_CHARGER_MASK)) {
			ctx->battery_responsive = 0;
			/*
			 * Try to revive ultra low voltage pack.  Charge
			 * battery pack with minimum current and maximum
			 * voltage for 30 seconds.
			 */
			charger_set_voltage(ctx->battery->voltage_max);
			charger_set_current(ctx->battery->precharge_current);
			for (d = 0; d < 30; d++) {
				sleep(1);
				rv = battery_temperature(&batt->temperature);
				if (rv == 0) {
					ctx->battery_responsive = 1;
					break;
				}
			}
		}

		/* Set error if battery is still unresponsive */
		if (rv) {
			curr->error |= F_BATTERY_UNRESPONSIVE;
			return curr->error;
		}
	} else {
		ctx->battery_responsive = 1;
	}

	if (battery_voltage(&batt->voltage))
		curr->error |= F_BATTERY_VOLTAGE;
	*ctx->memmap_batt_volt = batt->voltage;

	if (battery_current(&batt->current))
		curr->error |= F_BATTERY_CURRENT;
	/* Memory mapped value: discharge rate */
	*ctx->memmap_batt_rate = batt->current < 0 ?
		-batt->current : batt->current;

	if (battery_desired_voltage(&batt->desired_voltage))
		curr->error |= F_DESIRED_VOLTAGE;

	if (battery_desired_current(&batt->desired_current))
		curr->error |= F_DESIRED_CURRENT;

	if (fake_state_of_charge >= 0)
		batt->state_of_charge = fake_state_of_charge;
	else if (battery_state_of_charge(&batt->state_of_charge))
		curr->error |= F_BATTERY_STATE_OF_CHARGE;

	if (batt->state_of_charge != prev->batt.state_of_charge) {
		rv = battery_full_charge_capacity(&d);
		if (!rv && d != *(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC)) {
			*(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC) = d;
			/* Notify host to re-read battery information */
			host_set_single_event(EC_HOST_EVENT_BATTERY);
		}
	}

	/* Prevent deep discharging */
	if (!curr->ac) {
		if ((batt->state_of_charge < BATTERY_LEVEL_SHUTDOWN &&
		     !(curr->error & F_BATTERY_STATE_OF_CHARGE)) ||
		    (batt->voltage <= ctx->battery->voltage_min &&
		     !(curr->error & F_BATTERY_VOLTAGE)))
			low_battery_shutdown(ctx);
	}

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
	} else {
		*ctx->memmap_batt_flags &= ~EC_BATT_FLAG_LEVEL_CRITICAL;
	}

	/* Apply battery pack vendor charging method */
	battery_vendor_params(batt);

#ifdef CONFIG_CHARGING_CURRENT_LIMIT
	if (batt->desired_current > CONFIG_CHARGING_CURRENT_LIMIT)
		batt->desired_current = CONFIG_CHARGING_CURRENT_LIMIT;
#endif
	if (batt->desired_current > user_current_limit)
		batt->desired_current = user_current_limit;

	if (battery_get_battery_mode(&d)) {
		curr->error |= F_BATTERY_MODE;
	} else if (d & MODE_CAPACITY) {
		/* Battery capacity mode was set to mW; reset it back to mAh */
		if (battery_set_battery_mode(d & ~MODE_CAPACITY))
			ctx->curr.error |= F_BATTERY_MODE;
	}

	if (fake_state_of_charge >= 0)
		*ctx->memmap_batt_cap =
			fake_state_of_charge *
			*(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC) / 100;
	else if (battery_remaining_capacity(&d))
		ctx->curr.error |= F_BATTERY_CAPACITY;
	else
		*ctx->memmap_batt_cap = d;

	return ctx->curr.error;
}

/**
 * Init state handler
 *
 *	- check ac, charger, battery and temperature
 *	- initialize charger
 *	- new states: DISCHARGE, IDLE
 */
static enum power_state state_init(struct power_state_context *ctx)
{
	/* Stop charger, unconditionally */
	charger_set_current(0);
	charger_set_voltage(0);

	/* Update static battery info */
	update_battery_info();

	/* Clear shutdown timer */
	ctx->shutdown_warning_time.val = 0;

	/* If AC is not present, switch to discharging state */
	if (!ctx->curr.ac)
		return PWR_STATE_DISCHARGE;

	/* Check general error conditions */
	if (ctx->curr.error)
		return PWR_STATE_ERROR;

	/* Send battery event to host */
	host_set_single_event(EC_HOST_EVENT_BATTERY);

	return PWR_STATE_IDLE0;
}

/**
 * Idle state handler
 *
 *	- both charger and battery are online
 *	- detect charger and battery status change
 *	- new states: CHARGE, INIT
 */
static enum power_state state_idle(struct power_state_context *ctx)
{
	struct batt_params *batt = &ctx->curr.batt;

	/* If we are forcing idle mode, then just stay in IDLE. */
	if (state_machine_force_idle)
		return PWR_STATE_UNCHANGE;

	if (!ctx->curr.ac)
		return PWR_STATE_REINIT;

	if (ctx->curr.error)
		return PWR_STATE_ERROR;

	/* Prevent charging in idle mode */
	if (ctx->curr.charging_voltage ||
	    ctx->curr.charging_current)
		return PWR_STATE_REINIT;

	if (batt->state_of_charge >= BATTERY_LEVEL_FULL)
		return PWR_STATE_UNCHANGE;

	/* Configure init charger state and switch to charge state */
	if (batt->desired_voltage && batt->desired_current) {
		int want_current =
			charger_closest_current(batt->desired_current);

		CPRINTF("[%T Charge start %dmV %dmA]\n",
			batt->desired_voltage, want_current);

		if (charger_set_voltage(batt->desired_voltage) ||
		    charger_set_current(want_current))
			return PWR_STATE_ERROR;

		update_charger_time(ctx, get_time());

		if (ctx->curr.batt.state_of_charge < BATTERY_LEVEL_NEAR_FULL)
			return PWR_STATE_CHARGE;
		else
			return PWR_STATE_CHARGE_NEAR_FULL;
	}

	return PWR_STATE_UNCHANGE;
}

/**
 * Charge state handler
 *
 *	- detect battery status change
 *	- new state: INIT
 */
static enum power_state state_charge(struct power_state_context *ctx)
{
	struct power_state_data *curr = &ctx->curr;
	struct batt_params *batt = &ctx->curr.batt;
	int debounce = 0;
	int want_current;
	int want_voltage;
	timestamp_t now;

	if (curr->error)
		return PWR_STATE_ERROR;

	/* Check charger reset */
	if (curr->charging_voltage == 0 ||
	    curr->charging_current == 0)
		return PWR_STATE_REINIT;

	if (!curr->ac)
		return PWR_STATE_REINIT;

	if (batt->state_of_charge >= BATTERY_LEVEL_FULL) {
		if (charger_set_voltage(0) || charger_set_current(0))
			return PWR_STATE_ERROR;
		return PWR_STATE_IDLE;
	}

	now = get_time();

	/*
	 * Adjust desired voltage to one the charger can actually supply
	 * or else we'll keep asking for a voltage the charger can't actually
	 * supply.
	 */
	want_voltage = charger_closest_voltage(batt->desired_voltage);

	if (want_voltage != curr->charging_voltage) {
		CPRINTF("[%T Charge voltage %dmV]\n", want_voltage);
		if (charger_set_voltage(want_voltage))
			return PWR_STATE_ERROR;
		update_charger_time(ctx, now);
	}

	/*
	 * Adjust desired current to one the charger can actually supply before
	 * we do debouncing, or else we'll keep asking for a current the
	 * charger can't actually supply.
	 */
	want_current = charger_closest_current(batt->desired_current);

	if (want_current == curr->charging_current) {
		/* Tick charger watchdog */
		if (!is_charger_expired(ctx, now))
			return PWR_STATE_UNCHANGE;
	} else if (want_current > curr->charging_current) {
		if (!timestamp_expired(ctx->voltage_debounce_time, &now))
			return PWR_STATE_UNCHANGE;
	} else {
		debounce = 1;
	}

	if (want_current != curr->charging_current) {
		CPRINTF("[%T Charge current %dmA @ %dmV]\n",
			want_current, batt->desired_voltage);
	}

	if (charger_set_current(want_current))
		return PWR_STATE_ERROR;

	/* Update charger watchdog timer and debounce timer */
	update_charger_time(ctx, now);
	if (debounce)
		ctx->voltage_debounce_time.val = now.val + DEBOUNCE_TIME;

	return PWR_STATE_UNCHANGE;
}

/**
 * Discharge state handler
 *
 *	- detect ac status
 *	- new state: INIT
 */
static enum power_state state_discharge(struct power_state_context *ctx)
{
	struct batt_params *batt = &ctx->curr.batt;
	if (ctx->curr.ac)
		return PWR_STATE_REINIT;

	if (ctx->curr.error)
		return PWR_STATE_ERROR;

	/* Handle overtemp in discharging state by powering off host */
	if ((batt->temperature > ctx->battery->temp_discharge_max ||
	     batt->temperature < ctx->battery->temp_discharge_min) &&
	    chipset_in_state(CHIPSET_STATE_ON)) {
		CPRINTF("[%T charge force shutdown due to battery temp]\n");
		chipset_force_shutdown();
		host_set_single_event(EC_HOST_EVENT_BATTERY_SHUTDOWN);
	}
	return PWR_STATE_UNCHANGE;
}

/**
 * Error state handler
 *
 *	- check charger and battery communication
 *	- log error
 *	- new state: INIT
 */
static enum power_state state_error(struct power_state_context *ctx)
{
	static int logged_error;

	if (!ctx->curr.error) {
		logged_error = 0;
		return PWR_STATE_REINIT;
	}

	/* Debug output */
	if (ctx->curr.error != logged_error) {
		CPRINTF("[%T Charge error: flag[%08b -> %08b], ac %d, "
			" charger %s, battery %s\n",
			logged_error, ctx->curr.error, ctx->curr.ac,
			(ctx->curr.error & F_CHARGER_MASK) ? "(err)" : "ok",
			(ctx->curr.error & F_BATTERY_MASK) ? "(err)" : "ok");

		logged_error = ctx->curr.error;
	}

	return PWR_STATE_UNCHANGE;
}

/**
 * Print charging progress
 */
static void charging_progress(struct power_state_context *ctx)
{
	int seconds, minutes;

	if (ctx->curr.batt.state_of_charge != ctx->prev.batt.state_of_charge) {
		if (ctx->curr.ac)
			battery_time_to_full(&minutes);
		else
			battery_time_to_empty(&minutes);

		CPRINTF("[%T Battery %3d%% / %dh:%d]\n",
			ctx->curr.batt.state_of_charge,
			minutes / 60, minutes % 60);
		return;
	}

	if (ctx->curr.charging_voltage != ctx->prev.charging_voltage &&
	    ctx->trickle_charging_time.val) {
		/* Calculate minutes by dividing usec by 60 million.  GNU
		 * toolchain generates architecture dependent calls instead of
		 * machine code when the divisor is large, so break the
		 * calculation into 2 lines.
		 */
		seconds = (int)(get_time().val -
				ctx->trickle_charging_time.val) / (int)SECOND;
		minutes = seconds / 60;
		CPRINTF("[%T Precharge CHG(%dmV) BATT(%dmV %dmA) "
			"%dh:%d]\n", ctx->curr.charging_voltage,
			ctx->curr.batt.voltage, ctx->curr.batt.current,
			minutes / 60, minutes % 60);
	}
}

enum power_state charge_get_state(void)
{
	return task_ctx.curr.state;
}

uint32_t charge_get_flags(void)
{
	uint32_t flags = 0;

	if (state_machine_force_idle)
		flags |= CHARGE_FLAG_FORCE_IDLE;
	if (extpower_is_present())
		flags |= CHARGE_FLAG_EXTERNAL_POWER;

	return flags;
}

int charge_get_percent(void)
{
	return task_ctx.curr.batt.state_of_charge;
}

int charge_want_shutdown(void)
{
	return (charge_get_state() == PWR_STATE_DISCHARGE) &&
		charge_get_percent() < BATTERY_LEVEL_SHUTDOWN;
}

static int charge_force_idle(int enable)
{
	if (enable) {
		/*
		 * Force-idle state is only meaningful if external power is
		 * present. If it's not present we can't charge anyway...
		 */
		if (!(charge_get_flags() & CHARGE_FLAG_EXTERNAL_POWER))
			return EC_ERROR_UNKNOWN;
		charger_post_init();
	}
	state_machine_force_idle = enable;
	return EC_SUCCESS;
}

/**
 * Battery charging task
 */
void charger_task(void)
{
	struct power_state_context *ctx = &task_ctx;
	timestamp_t ts;
	int sleep_usec = POLL_PERIOD_SHORT, diff_usec, sleep_next;
	enum power_state new_state;
	uint8_t batt_flags;

	while (1) {
		state_common(ctx);

		switch (ctx->prev.state) {
		case PWR_STATE_INIT:
		case PWR_STATE_REINIT:
			new_state = state_init(ctx);
			break;
		case PWR_STATE_IDLE0:
			new_state = state_idle(ctx);
			/* If still idling, move from IDLE0 to IDLE */
			if (new_state == PWR_STATE_UNCHANGE)
				new_state = PWR_STATE_IDLE;
			break;
		case PWR_STATE_IDLE:
			new_state = state_idle(ctx);
			break;
		case PWR_STATE_DISCHARGE:
			new_state = state_discharge(ctx);
			break;
		case PWR_STATE_CHARGE:
			new_state = state_charge(ctx);
			if (new_state == PWR_STATE_UNCHANGE &&
			    (ctx->curr.batt.state_of_charge >=
			     BATTERY_LEVEL_NEAR_FULL)) {
				/* Almost done charging */
				new_state = PWR_STATE_CHARGE_NEAR_FULL;
			}
			break;

		case PWR_STATE_CHARGE_NEAR_FULL:
			new_state = state_charge(ctx);
			if (new_state == PWR_STATE_UNCHANGE &&
			    (ctx->curr.batt.state_of_charge <
			     BATTERY_LEVEL_NEAR_FULL)) {
				/* Battery below almost-full threshold. */
				new_state = PWR_STATE_CHARGE;
			}
			break;
		case PWR_STATE_ERROR:
			new_state = state_error(ctx);
			break;
		default:
			CPRINTF("[%T Charge state %d undefined]\n",
				ctx->curr.state);
			ctx->curr.state = PWR_STATE_ERROR;
			new_state = PWR_STATE_ERROR;
		}

		if (state_machine_force_idle &&
		    ctx->prev.state != PWR_STATE_IDLE0 &&
		    ctx->prev.state != PWR_STATE_IDLE &&
		    ctx->prev.state != PWR_STATE_INIT &&
		    ctx->prev.state != PWR_STATE_REINIT)
			new_state = PWR_STATE_REINIT;

		if (new_state) {
			ctx->curr.state = new_state;
			CPRINTF("[%T Charge state %s -> %s]\n",
				state_name[ctx->prev.state],
				state_name[new_state]);
		}

#ifdef HAS_TASK_POWERBTN
		/*
		 * After first init, wake the power button task so it can
		 * power on the AP if necessary.
		 *
		 * TODO(rspangler): API, instead of assuming power button task
		 * exists
		 */
		if (ctx->prev.state == PWR_STATE_INIT &&
		    new_state != PWR_STATE_INIT)
			task_wake(TASK_ID_POWERBTN);
#endif

		switch (new_state) {
		case PWR_STATE_IDLE0:
			/*
			 * First time transitioning from init -> idle.  Don't
			 * set the flags or LED yet because we may transition
			 * to charging on the next call and we don't want to
			 * blink the LED green.
			 */
			sleep_usec = POLL_PERIOD_SHORT;
			break;
		case PWR_STATE_CHARGE_NEAR_FULL:
			/*
			 * Battery is almost charged.  The last few percent
			 * take a loooong time, so fall through and look like
			 * we're charged.  This mirrors similar hacks at the
			 * ACPI/kernel/UI level.
			 */
		case PWR_STATE_IDLE:
			batt_flags = *ctx->memmap_batt_flags;
			batt_flags &= ~EC_BATT_FLAG_CHARGING;
			batt_flags &= ~EC_BATT_FLAG_DISCHARGING;
			*ctx->memmap_batt_flags = batt_flags;

			/* Charge done */
			sleep_usec = (new_state == PWR_STATE_IDLE ?
				      POLL_PERIOD_LONG : POLL_PERIOD_CHARGE);
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
			sleep_usec = POLL_PERIOD_CHARGE;
			break;
		case PWR_STATE_ERROR:
			/* Error */
			sleep_usec = POLL_PERIOD_CHARGE;
			break;
		case PWR_STATE_UNCHANGE:
			/* Don't change sleep duration */
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

		if (ctx->curr.state == PWR_STATE_DISCHARGE &&
		    chipset_in_state(CHIPSET_STATE_ANY_OFF |
				     CHIPSET_STATE_SUSPEND)) {
			/*
			 * Discharging and system is off or suspended, so no
			 * need to poll frequently.  charge_hook() will wake us
			 * up if anything important changes.
			 */
			sleep_next = POLL_PERIOD_VERY_LONG - diff_usec;
		} else if (sleep_next < MIN_SLEEP_USEC) {
			sleep_next = MIN_SLEEP_USEC;
		} else if (sleep_next > MAX_SLEEP_USEC) {
			sleep_next = MAX_SLEEP_USEC;
		}

		task_wait_event(sleep_next);
	}
}

/*****************************************************************************/
/* Hooks */

/**
 * Charge notification hook.
 *
 * This is triggered when the AC state changes or the system boots, so that
 * we can update our charging state.
 */
static void charge_hook(void)
{
	/* Wake up the task now */
	task_wake(TASK_ID_CHARGER);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, charge_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_AC_CHANGE, charge_hook, HOOK_PRIO_DEFAULT);

static void charge_init(void)
{
	struct power_state_context *ctx = &task_ctx;

	ctx->prev.state = PWR_STATE_INIT;
	ctx->curr.state = PWR_STATE_INIT;
	ctx->trickle_charging_time.val = 0;
	ctx->battery = battery_get_info();
	ctx->charger = charger_get_info();
	/* Assume the battery is responsive until proven otherwise */
	ctx->battery_responsive = 1;

	/* Set up LPC direct memmap */
	ctx->memmap_batt_volt =
		(uint32_t *)host_get_memmap(EC_MEMMAP_BATT_VOLT);
	ctx->memmap_batt_rate =
		(uint32_t *)host_get_memmap(EC_MEMMAP_BATT_RATE);
	ctx->memmap_batt_cap =
		(uint32_t *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	ctx->memmap_batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);
}
DECLARE_HOOK(HOOK_INIT, charge_init, HOOK_PRIO_DEFAULT);


static void charge_shutdown(void)
{
	/* Hibernate immediately if battery level is too low */
	if (charge_want_shutdown()) {
		CPRINTF("[%T charge force EC hibernate after"
			" shutdown due to low battery]\n");
		system_hibernate(0, 0);
	}
}
/*
 * Run the charge shutdown hook last, since when it hibernates no subsequent
 * hooks would be run.
 */
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, charge_shutdown, HOOK_PRIO_LAST);

/*****************************************************************************/
/* Host commands */

static int charge_command_charge_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_charge_control *p = args->params;
	int rv;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	rv = charge_force_idle(p->mode != CHARGE_CONTROL_NORMAL);
	if (rv != EC_SUCCESS)
		return rv;

#ifdef CONFIG_CMD_DISCHARGE_ON_AC
	rv = board_discharge_on_ac(p->mode == CHARGE_CONTROL_DISCHARGE);
	if (rv != EC_SUCCESS)
		return rv;
#endif /* CONFIG_CMD_DISCHARGE_ON_AC */

	return EC_RES_SUCCESS;
}
/*
 * TODO(crbug.com/239197) : Adding both versions to the version mask is a
 * temporary workaround for a problem in the cros_ec driver. Drop
 * EC_VER_MASK(0) once cros_ec driver can send the correct version.
 */
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_CONTROL, charge_command_charge_control,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static int charge_command_dump(struct host_cmd_handler_args *args)
{
	char *dest = (char *)args->response;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	ASSERT(sizeof(task_ctx) <= args->response_max);

	memcpy(dest, &task_ctx, sizeof(task_ctx));
	args->response_size = sizeof(task_ctx);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_DUMP, charge_command_dump,
		     EC_VER_MASK(0));

static void reset_current_limit(void)
{
	user_current_limit = -1;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, reset_current_limit, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, reset_current_limit, HOOK_PRIO_DEFAULT);

static int charge_command_current_limit(struct host_cmd_handler_args *args)
{
	const struct ec_params_current_limit *p = args->params;

	user_current_limit = p->limit;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_CURRENT_LIMIT, charge_command_current_limit,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

static int command_battfake(int argc, char **argv)
{
	char *e;
	int v;

	if (argc == 2) {
		v = strtoi(argv[1], &e, 0);
		if (*e || v < -1 || v > 100)
			return EC_ERROR_PARAM1;

		fake_state_of_charge = v;
	}

	if (fake_state_of_charge < 0)
		ccprintf("Reporting real battery level\n");
	else
		ccprintf("Reporting fake battery level %d%%\n",
			 fake_state_of_charge);

	/* Wake charger task immediately to see new level */
	task_wake(TASK_ID_CHARGER);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battfake, command_battfake,
			"percent (-1 = use real level)",
			"Set fake battery level",
			NULL);
