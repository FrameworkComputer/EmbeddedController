/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 */

#include "timer.h"

#ifndef __CROS_EC_CHARGE_STATE_H
#define __CROS_EC_CHARGE_STATE_H

/* Time constants */
#define MSEC    (1000ULL)
#define SECOND  (MSEC * 1000)
#define MINUTE  (SECOND * 60)
#define HOUR    (MINUTE * 60)

/* Update period to prevent charger watchdog timeout */
#define CHARGER_UPDATE_PERIOD (SECOND * 10)

/* Power state task polling period in usec */
#define POLL_PERIOD_VERY_LONG   MINUTE
#define POLL_PERIOD_LONG        (MSEC * 500)
#define POLL_PERIOD_CHARGE      (MSEC * 250)
#define POLL_PERIOD_SHORT       (MSEC * 100)
#define MIN_SLEEP_USEC          (MSEC * 50)
#define MAX_SLEEP_USEC          SECOND

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
	/* Meta-state; unchanged from previous time through task loop */
	PWR_STATE_UNCHANGE = 0,
	/* (Re-)initalizing charge state machine */
	PWR_STATE_INIT,
	/* Just transitioned from init to idle */
	PWR_STATE_IDLE0,
	/* Idle; AC present */
	PWR_STATE_IDLE,
	/* Discharging */
	PWR_STATE_DISCHARGE,
	/* Charging */
	PWR_STATE_CHARGE,
	/* Charging, almost fully charged */
	PWR_STATE_CHARGE_NEAR_FULL,
	/* Charging state machine error */
	PWR_STATE_ERROR
};

/* Debugging constants, in the same order as enum power_state. This string
 * table was moved here to sync with enum above.
 */
#define POWER_STATE_NAME_TABLE  \
	{			\
		"unchange",	\
		"init",		\
		"idle0",	\
		"idle",		\
		"discharge",	\
		"charge",	\
		"charge_near_full",      \
		"error"		\
	}
	/* End of POWER_STATE_NAME_TABLE macro */

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
	/* Charger and battery pack info */
	const struct charger_info *charger;
	const struct battery_info *battery;
	/* Charging timestamps */
	timestamp_t charger_update_time;
	timestamp_t trickle_charging_time;
	timestamp_t voltage_debounce_time;
};

/* Trickle charging state handler.
 * Trickle charging state is sub-state of charging. Normal charging handler
 * can not set battery input current cap to a very low value. This function
 * uses charging voltage to control battery input current.
 */
enum power_state trickle_charge(struct power_state_context *ctx);

/**
 * Return current charge state.
 */
enum power_state charge_get_state(void);

/**
 * Return current battery charge percentage.
 */
int charge_get_percent(void);

#endif /* __CROS_EC_CHARGE_STATE_H */

