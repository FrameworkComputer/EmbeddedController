/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "timer.h"

#ifndef __CROS_EC_CHARGE_STATE_H
#define __CROS_EC_CHARGE_STATE_H

/* Update period to prevent charger watchdog timeout */
#define CHARGER_UPDATE_PERIOD (SECOND * 10)

/* Power state task polling periods in usec */
#define POLL_PERIOD_VERY_LONG   MINUTE
#define POLL_PERIOD_LONG        (MSEC * 500)
#define POLL_PERIOD_CHARGE      (MSEC * 250)
#define POLL_PERIOD_SHORT       (MSEC * 100)
#define MIN_SLEEP_USEC          (MSEC * 50)
#define MAX_SLEEP_USEC          SECOND

/* Power state error flags */
#define F_CHARGER_INIT        (1 << 0) /* Charger initialization */
#define F_CHARGER_VOLTAGE     (1 << 1) /* Charger maximum output voltage */
#define F_CHARGER_CURRENT     (1 << 2) /* Charger maximum output current */
#define F_BATTERY_VOLTAGE     (1 << 3) /* Battery voltage */
#define F_BATTERY_MODE        (1 << 8) /* Battery mode */
#define F_BATTERY_CAPACITY    (1 << 9) /* Battery capacity */
#define F_BATTERY_STATE_OF_CHARGE (1 << 10) /* State of charge, percentage */
#define F_BATTERY_UNRESPONSIVE    (1 << 11) /* Battery not responding */
#define F_BATTERY_NOT_CONNECTED   (1 << 12) /* Battery not connected */
#define F_BATTERY_GET_PARAMS  (1 << 13) /* Any battery parameter bad */

#define F_BATTERY_MASK (F_BATTERY_VOLTAGE | \
			F_BATTERY_MODE | \
			F_BATTERY_CAPACITY | F_BATTERY_STATE_OF_CHARGE | \
			F_BATTERY_UNRESPONSIVE | F_BATTERY_NOT_CONNECTED | \
			F_BATTERY_GET_PARAMS)
#define F_CHARGER_MASK (F_CHARGER_VOLTAGE | F_CHARGER_CURRENT | \
			F_CHARGER_INIT)

/* Power states */
enum charge_state {
	/* Meta-state; unchanged from previous time through task loop */
	PWR_STATE_UNCHANGE = 0,
	/* Initializing charge state machine at boot */
	PWR_STATE_INIT,
	/* Re-initializing charge state machine */
	PWR_STATE_REINIT,
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

/* Charge state flags */
/* Forcing idle state */
#define CHARGE_FLAG_FORCE_IDLE (1 << 0)
/* External (AC) power is present */
#define CHARGE_FLAG_EXTERNAL_POWER (1 << 1)

/* Debugging constants, in the same order as enum charge_state. This string
 * table was moved here to sync with enum above.
 */
#define CHARGE_STATE_NAME_TABLE { \
		"unchange",	\
		"init",		\
		"reinit",	\
		"idle0",	\
		"idle",		\
		"discharge",	\
		"charge",	\
		"charge_near_full",      \
		"error"		\
	}
	/* End of CHARGE_STATE_NAME_TABLE macro */

/* Power state data
 * Status collection of charging state machine.
 */
struct charge_state_data {
	int ac;
	int charging_voltage;
	int charging_current;
	struct batt_params batt;
	enum charge_state state;
	uint32_t error;
	timestamp_t ts;
};

/* State context
 * The shared context for state handler. The context contains current and
 * previous state.
 */
struct charge_state_context {
	struct charge_state_data curr;
	struct charge_state_data prev;
	timestamp_t charge_state_updated_time;
	uint32_t *memmap_batt_volt;
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
	timestamp_t shutdown_warning_time;
	int battery_responsive;
};

/**
 * Return current charge state.
 */
enum charge_state charge_get_state(void);

/**
 * Return current charge state flags (CHARGE_FLAG_*)
 */
uint32_t charge_get_flags(void);

/**
 * Return current battery charge percentage.
 */
int charge_get_percent(void);

/**
 * Return non-zero if discharging and battery so low we should shut down.
 */
#ifdef CONFIG_CHARGER
int charge_want_shutdown(void);
#else
static inline int charge_want_shutdown(void) { return 0; }
#endif

/**
 * Get the last polled battery/charger temperature.
 *
 * @param idx		Sensor index to read.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int charge_temp_sensor_get_val(int idx, int *temp_ptr);

#endif /* __CROS_EC_CHARGE_STATE_H */

