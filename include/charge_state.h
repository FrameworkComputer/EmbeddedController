/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_CHARGE_STATE_H
#define __CROS_EC_CHARGE_STATE_H

#include "common.h"
#include "timer.h"

/* Stuff that's common to all charger implementations can go here. */

/* Seconds to spend trying to wake a non-responsive battery */
#define PRECHARGE_TIMEOUT 30

/* Power state task polling periods in usec */
#define CHARGE_POLL_PERIOD_VERY_LONG   MINUTE
#define CHARGE_POLL_PERIOD_LONG        (MSEC * 500)
#define CHARGE_POLL_PERIOD_CHARGE      (MSEC * 250)
#define CHARGE_POLL_PERIOD_SHORT       (MSEC * 100)
#define CHARGE_MIN_SLEEP_USEC          (MSEC * 50)
#define CHARGE_MAX_SLEEP_USEC          SECOND

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
/* Battery is responsive */
#define CHARGE_FLAG_BATT_RESPONSIVE (1 << 2)

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


/**
 * Return current charge state.
 */
enum charge_state charge_get_state(void);

/**
 * Return non-zero if battery is so low we want to keep AP off.
 */
int charge_keep_power_off(void);

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
int charge_want_shutdown(void);

/**
 * Return non-zero if the battery level is too low to allow power on, even if
 * a charger is attached.
 */
int charge_prevent_power_on(void);

/**
 * Get the last polled battery/charger temperature.
 *
 * @param idx		Sensor index to read.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int charge_temp_sensor_get_val(int idx, int *temp_ptr);

/**
 * Get the pointer to the battery parameters we saved in charge state.
 *
 * Use this carefully. Other threads can modify data while you are reading.
 */
const struct batt_params *charger_current_battery_params(void);


/* Pick the right implementation */
#ifdef CONFIG_CHARGER_V1
#ifdef CONFIG_CHARGER_V2
#error "Choose either CONFIG_CHARGER_V1 or CONFIG_CHARGER_V2, not both"
#else
#include "charge_state_v1.h"
#endif
#else  /* not V1 */
#ifdef CONFIG_CHARGER_V2
#include "charge_state_v2.h"
#endif
#endif	/* CONFIG_CHARGER_V1 */

#endif	/* __CROS_EC_CHARGE_STATE_H */
