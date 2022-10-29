/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_CHARGE_STATE_H
#define __CROS_EC_CHARGE_STATE_H

#include "common.h"
#include "timer.h"
#include "stdbool.h"

/* Stuff that's common to all charger implementations can go here. */

/* Seconds to spend trying to wake a non-responsive battery */
#define PRECHARGE_TIMEOUT CONFIG_BATTERY_PRECHARGE_TIMEOUT

/* Power state task polling periods in usec */
#define CHARGE_POLL_PERIOD_VERY_LONG MINUTE
#define CHARGE_POLL_PERIOD_LONG (MSEC * 500)
#define CHARGE_POLL_PERIOD_CHARGE (MSEC * 250)
#define CHARGE_POLL_PERIOD_SHORT (MSEC * 100)
#define CHARGE_MIN_SLEEP_USEC (MSEC * 50)
/* If a board hasn't provided a max sleep, use 1 minute as default */
#ifndef CHARGE_MAX_SLEEP_USEC
#define CHARGE_MAX_SLEEP_USEC MINUTE
#endif

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
	/* Forced Idle */
	PWR_STATE_FORCED_IDLE,
	/* Discharging */
	PWR_STATE_DISCHARGE,
	/* Discharging and fully charged */
	PWR_STATE_DISCHARGE_FULL,
	/* Charging */
	PWR_STATE_CHARGE,
	/* Charging, almost fully charged */
	PWR_STATE_CHARGE_NEAR_FULL,
	/* Charging state machine error */
	PWR_STATE_ERROR,
	/*  Count of total states */
	PWR_STATE_COUNT
};

/* Charge state flags */
/* Forcing idle state */
#define CHARGE_FLAG_FORCE_IDLE BIT(0)
/* External (AC) power is present */
#define CHARGE_FLAG_EXTERNAL_POWER BIT(1)
/* Battery is responsive */
#define CHARGE_FLAG_BATT_RESPONSIVE BIT(2)

/* Debugging constants, in the same order as enum charge_state. This string
 * table was moved here to sync with enum above.
 */
#define CHARGE_STATE_NAME_TABLE                                             \
	{                                                                   \
		"unchange", "init", "reinit", "idle0", "idle", "discharge", \
			"discharge_full", "charge", "charge_near_full",     \
			"error"                                             \
	}
/* End of CHARGE_STATE_NAME_TABLE macro */

/**
 * Return current charge state.
 */
enum charge_state charge_get_state(void);

/**
 * Return current charge v2 state.
 */
__test_only enum charge_state_v2 charge_get_state_v2(void);

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
#if defined(CONFIG_BATTERY) || defined(TEST_BUILD)
int charge_get_percent(void);
#else
static inline int charge_get_percent(void)
{
	return 0;
}
#endif

/**
 * Return current battery charge if not using charge manager sub-system.
 */
int board_get_battery_soc(void);

/**
 * Return current display charge in 10ths of a percent (e.g. 1000 = 100.0%)
 */
int charge_get_display_charge(void);

/**
 * Check if board is consuming full input current
 *
 * This returns true if the battery charge percentage is between 2% and 95%
 * exclusive.
 *
 * @return Board is consuming full input current
 */
__override_proto int charge_is_consuming_full_input_current(void);

/**
 * Return non-zero if discharging and battery so low we should shut down.
 */
#if defined(CONFIG_CHARGER) && defined(CONFIG_BATTERY)
int charge_want_shutdown(void);
#else
static inline int charge_want_shutdown(void)
{
	return 0;
}
#endif

/**
 * Return true if battery level is below threshold, false otherwise,
 * or if SoC can't be determined.
 *
 * @param transitioned	True to check if SoC is previously above threshold
 */
enum batt_threshold_type {
	BATT_THRESHOLD_TYPE_LOW = 0,
	BATT_THRESHOLD_TYPE_SHUTDOWN
};
#if defined(CONFIG_CHARGER) && defined(CONFIG_BATTERY)
int battery_is_below_threshold(enum batt_threshold_type type,
			       bool transitioned);
#else
static inline int battery_is_below_threshold(enum batt_threshold_type type,
					     bool transitioned)
{
	return 0;
}
#endif

/**
 * Return non-zero if the battery level is too low to allow power on, even if
 * a charger is attached.
 *
 * @param power_button_pressed	True if the power-up attempt is caused by a
 *				power button press.
 */
#ifdef CONFIG_BATTERY
int charge_prevent_power_on(int power_button_pressed);
#else
static inline int charge_prevent_power_on(int power_button_pressed)
{
	return 0;
}
#endif

/**
 * Get the last polled battery/charger temperature.
 *
 * @param idx		Sensor index to read.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int charge_get_battery_temp(int idx, int *temp_ptr);

/**
 * Get the pointer to the battery parameters we saved in charge state.
 *
 * Use this carefully. Other threads can modify data while you are reading.
 */
const struct batt_params *charger_current_battery_params(void);

/* Config Charger */
#include "charge_state_v2.h"

#ifdef CONFIG_EMI_REGION1
void battery_customize(struct charge_state_data *emi_info);
#endif

#endif /* __CROS_EC_CHARGE_STATE_H */
