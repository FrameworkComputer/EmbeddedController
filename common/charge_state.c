/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging task and state machine.
 */


#include "battery.h"
#include "battery_pack.h"
#include "board.h"
#include "console.h"
#include "charger.h"
#include "gpio.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "power_led.h"
#include "smart_battery.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Stop charge when state of charge reaches this percentage */
#define STOP_CHARGE_THRESHOLD 100

/* power state task polling period in usec */
#define POLL_PERIOD_LONG        500000
#define POLL_PERIOD_CHARGE      250000
#define POLL_PERIOD_SHORT       100000
#define MIN_SLEEP_USEC          50000

/* Power state error flags */
#define F_CHARGER_INIT        (1 << 0) /* Charger initialization */
#define F_CHARGER_VOLTAGE     (1 << 1) /* Charger maximun output voltage */
#define F_CHARGER_CURRENT     (1 << 2) /* Charger maximum output current */
#define F_BATTERY_VOLTAGE     (1 << 3) /* Battery voltage */
#define F_BATTERY_CURRENT     (1 << 4) /* Battery charging current */
#define F_DESIRED_VOLTAGE     (1 << 5) /* Battery desired voltage */
#define F_DESIRED_CURRENT     (1 << 6) /* Battery desired current */
#define F_BATTERY_TEMPERATURE (1 << 7) /* Battery temperature */
#define F_BATTERY_MODE        (1 << 8) /* Battery mode */
#define F_BATTERY_CAPACITY    (1 << 9) /* Battery capacity */
#define F_BATTERY_STATE_OF_CHARGE (1 << 10) /* State of charge, percentage */

#define F_BATTERY_MASK (F_BATTERY_VOLTAGE | F_BATTERY_CURRENT |  \
			F_DESIRED_VOLTAGE | F_DESIRED_CURRENT |  \
			F_BATTERY_TEMPERATURE | F_BATTERY_MODE | \
			F_BATTERY_CAPACITY | F_BATTERY_STATE_OF_CHARGE)
#define F_CHARGER_MASK (F_CHARGER_VOLTAGE | F_CHARGER_CURRENT | \
			F_CHARGER_INIT)

/* Power states */
enum power_state {
	PWR_STATE_UNCHANGE = 0,
	PWR_STATE_INIT,
	PWR_STATE_IDLE,
	PWR_STATE_DISCHARGE,
	PWR_STATE_CHARGE,
	PWR_STATE_ERROR
};
/* Debugging constants, in the same order as power_state. */
static const char * const state_name[] = {
	"unchange",
	"init",
	"idle",
	"discharge",
	"charge",
	"error"
};

/* Power state data
 * Status collection of charging state machine.
 */
struct power_state_data {
	int ac;
	int charging_voltage;
	int charging_current;
	struct batt_params batt;
	enum power_state state;
	uint32_t error;
	timestamp_t ts;
};

/* State context
 * The shared context for state handler. The context contains current and
 * previous state.
 */
struct power_state_context {
	struct power_state_data curr;
	struct power_state_data prev;
	uint32_t *memmap_batt_volt;
	/* TODO(rong): check endianness of EC and memmap*/
	uint32_t *memmap_batt_rate;
	uint32_t *memmap_batt_cap;
	uint8_t *memmap_batt_flags;
};

/* helper function(s) */
static inline int get_ac(void)
{
	return gpio_get_level(GPIO_AC_PRESENT);
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
	curr->ac = get_ac();
	if (curr->ac != prev->ac) {
		if (curr->ac) {
			/* AC on
			 *   Initialize charger to power on reset mode
			 * TODO(rong):
			 * crosbug.com/p/7937 Send PCH AC present
			 */
			rv = charger_post_init();
			if (rv)
				curr->error |= F_CHARGER_INIT;
			lpc_set_host_events(EC_LPC_HOST_EVENT_MASK(
				EC_LPC_HOST_EVENT_AC_CONNECTED));
		} else {
			/* AC off */
			lpc_set_host_events(EC_LPC_HOST_EVENT_MASK(
				EC_LPC_HOST_EVENT_AC_DISCONNECTED));
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
	} else
		*batt_flags &= ~EC_BATT_FLAG_AC_PRESENT;

	rv = battery_temperature(&batt->temperature);
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
	*ctx->memmap_batt_rate = batt->current < 0 ? -batt->current : 0;

	rv = battery_desired_voltage(&batt->desired_voltage);
	if (rv)
		curr->error |= F_DESIRED_VOLTAGE;

	rv = battery_desired_current(&batt->desired_current);
	if (rv)
		curr->error |= F_DESIRED_CURRENT;

	rv = battery_state_of_charge(&batt->state_of_charge);
	if (rv)
		curr->error |= F_BATTERY_STATE_OF_CHARGE;

	/* Check battery presence */
	if (curr->error & F_BATTERY_MASK) {
		*ctx->memmap_batt_flags &= ~EC_BATT_FLAG_BATT_PRESENT;
		return curr->error;
	}

	*ctx->memmap_batt_flags |= EC_BATT_FLAG_BATT_PRESENT;

	/* Battery charge level low */
	if (batt->state_of_charge <= BATTERY_LEVEL_LOW &&
			prev->batt.state_of_charge > BATTERY_LEVEL_LOW)
		lpc_set_host_events(EC_LPC_HOST_EVENT_MASK(
			EC_LPC_HOST_EVENT_BATTERY_LOW));

	/* Battery charge level critical */
	if (batt->state_of_charge <= BATTERY_LEVEL_CRITICAL) {
		*ctx->memmap_batt_flags |= EC_BATT_FLAG_LEVEL_CRITICAL;
		/* Send battery critical host event */
		if (prev->batt.state_of_charge > BATTERY_LEVEL_CRITICAL)
			lpc_set_host_events(EC_LPC_HOST_EVENT_MASK(
					EC_LPC_HOST_EVENT_BATTERY_CRITICAL));
	} else
		*ctx->memmap_batt_flags &= ~EC_BATT_FLAG_LEVEL_CRITICAL;


	/* Apply battery pack vendor charging method */
	battery_vendor_params(batt);

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

	return PWR_STATE_IDLE;
}

/* Idle state handler
 *	- both charger and battery are online
 *	- detect charger and battery status change
 *	- new states: CHARGE, INIT
 */
static enum power_state state_idle(struct power_state_context *ctx)
{
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
		if (charger_set_voltage(ctx->curr.batt.desired_voltage))
			return PWR_STATE_ERROR;
		if (charger_set_current(ctx->curr.batt.desired_current))
			return PWR_STATE_ERROR;
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
	if (!ctx->curr.ac)
		return PWR_STATE_INIT;

	if (ctx->curr.error)
		return PWR_STATE_ERROR;

	/* Check charger reset */
	if (ctx->curr.charging_voltage == 0 ||
	    ctx->curr.charging_current == 0)
		return PWR_STATE_INIT;

	if (ctx->curr.batt.state_of_charge >= STOP_CHARGE_THRESHOLD) {
		if (charger_set_voltage(0) || charger_set_current(0))
			return PWR_STATE_ERROR;
		return PWR_STATE_IDLE;
	}

	if (ctx->curr.batt.desired_voltage != ctx->curr.charging_voltage)
		if (charger_set_voltage(ctx->curr.batt.desired_voltage))
			return PWR_STATE_ERROR;
	if (ctx->curr.batt.desired_current != ctx->curr.charging_current)
		if (charger_set_current(ctx->curr.batt.desired_current))
			return PWR_STATE_ERROR;

	return PWR_STATE_UNCHANGE;
}

/* Discharge state handler
 *	- detect ac status
 *	- new state: INIT
 */
static enum power_state state_discharge(struct power_state_context *ctx)
{
	if (ctx->curr.ac)
		return PWR_STATE_INIT;

	if (ctx->curr.error)
		return PWR_STATE_ERROR;

	/* TODO(rong): crosbug.com/p/8451
	 * handle overtemp in discharge mode
	 */

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
		uart_printf("[Charge error: flag[%08b -> %08b], ac %d, "
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
	int minutes;
	if (ctx->curr.batt.state_of_charge !=
	    ctx->prev.batt.state_of_charge) {
		if (ctx->curr.ac)
			battery_time_to_full(&minutes);
		else
			battery_time_to_empty(&minutes);

		uart_printf("[Battery %3d%% / %dh:%d]\n",
			ctx->curr.batt.state_of_charge,
			minutes / 60, minutes % 60);
	}
}

/* Battery charging task */
void charge_state_machine_task(void)
{
	struct power_state_context ctx;
	timestamp_t ts;
	int sleep_usec, diff_usec;
	enum power_state new_state;
	uint8_t batt_flags;

	ctx.prev.state = PWR_STATE_INIT;
	ctx.curr.state = PWR_STATE_INIT;

	/* Setup LPC direct memmap */
	ctx.memmap_batt_volt  = (uint32_t *)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_VOLT);
	ctx.memmap_batt_rate  = (uint32_t *)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_RATE);
	ctx.memmap_batt_cap   = (uint32_t *)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_CAP);
	ctx.memmap_batt_flags = (uint8_t *)(lpc_get_memmap_range() +
					EC_LPC_MEMMAP_BATT_FLAG);

	while (1) {

		state_common(&ctx);

		switch (ctx.prev.state) {
		case PWR_STATE_INIT:
			new_state = state_init(&ctx);
			break;
		case PWR_STATE_IDLE:
			new_state = state_idle(&ctx);
			break;
		case PWR_STATE_DISCHARGE:
			new_state = state_discharge(&ctx);
			break;
		case PWR_STATE_CHARGE:
			new_state = state_charge(&ctx);
			break;
		case PWR_STATE_ERROR:
			new_state = state_error(&ctx);
			break;
		default:
			uart_printf("[Undefined charging state %d]\n",
					ctx.curr.state);
			ctx.curr.state = PWR_STATE_ERROR;
			new_state = PWR_STATE_ERROR;
		}

		if (new_state) {
			ctx.curr.state = new_state;
			uart_printf("[Charge state %s -> %s]\n",
				state_name[ctx.prev.state],
				state_name[new_state]);
		}

		switch (new_state) {
		case PWR_STATE_IDLE:
			batt_flags = *ctx.memmap_batt_flags;
			batt_flags &= ~EC_BATT_FLAG_CHARGING;
			batt_flags &= ~EC_BATT_FLAG_DISCHARGING;
			*ctx.memmap_batt_flags = batt_flags;

			/* Charge done */
			powerled_set(POWERLED_GREEN);

			sleep_usec = POLL_PERIOD_LONG;
			break;
		case PWR_STATE_DISCHARGE:
			batt_flags = *ctx.memmap_batt_flags;
			batt_flags &= ~EC_BATT_FLAG_CHARGING;
			batt_flags |= EC_BATT_FLAG_DISCHARGING;
			*ctx.memmap_batt_flags = batt_flags;
			sleep_usec = POLL_PERIOD_LONG;
			break;
		case PWR_STATE_CHARGE:
			batt_flags = *ctx.memmap_batt_flags;
			batt_flags |= EC_BATT_FLAG_CHARGING;
			batt_flags &= ~EC_BATT_FLAG_DISCHARGING;
			*ctx.memmap_batt_flags = batt_flags;

			/* Charging */
			powerled_set(POWERLED_YELLOW);

			sleep_usec = POLL_PERIOD_CHARGE;
			break;
		case PWR_STATE_ERROR:
			/* Error */
			powerled_set(POWERLED_RED);

			sleep_usec = POLL_PERIOD_CHARGE;
		default:
			sleep_usec = POLL_PERIOD_SHORT;
		}

		/* Show charging progress in console */
		charging_progress(&ctx);

		ts = get_time();
		diff_usec = (int)(ts.val - ctx.curr.ts.val);
		sleep_usec -= diff_usec;

		if (sleep_usec < MIN_SLEEP_USEC)
			sleep_usec = MIN_SLEEP_USEC;

		usleep(sleep_usec);
	}
}

