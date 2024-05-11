/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_CHARGE_STATE_H
#define __CROS_EC_CHARGE_STATE_H

#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "ec_ec_comm_client.h"
#include "ocpc.h"
#include "stdbool.h"
#include "timer.h"

#ifdef __cplusplus
extern "C" {
#endif

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
enum led_pwr_state {
	/* Meta-state; unchanged from previous time through task loop */
	LED_PWRS_UNCHANGE = 0,
	/* Initializing charge state machine at boot */
	LED_PWRS_INIT,
	/* Re-initializing charge state machine */
	LED_PWRS_REINIT,
	/* Just transitioned from init to idle */
	LED_PWRS_IDLE0,
	/* Idle; AC present */
	LED_PWRS_IDLE,
	/* Forced Idle */
	LED_PWRS_FORCED_IDLE,
	/* Discharging */
	LED_PWRS_DISCHARGE,
	/* Discharging and fully charged */
	LED_PWRS_DISCHARGE_FULL,
	/* Charging */
	LED_PWRS_CHARGE,
	/* Charging, almost fully charged */
	LED_PWRS_CHARGE_NEAR_FULL,
	/* Charging state machine error */
	LED_PWRS_ERROR,
	/*  Count of total states */
	LED_PWRS_COUNT
};

/*
 * Charge state flags for LED control.
 * This is being deprecated. Use enum led_pwr_state instead.
 */
/* Forcing idle state */
#define CHARGE_LED_FLAG_FORCE_IDLE BIT(0)
/* External (AC) power is present */
#define CHARGE_LED_FLAG_EXTERNAL_POWER BIT(1)
/* Battery is responsive */
#define CHARGE_LED_FLAG_BATT_RESPONSIVE BIT(2)

/*
 * Charge task's states
 *
 * Use enum led_pwr_state for controlling LEDs.
 */
enum charge_state {
	ST_IDLE = 0,
	ST_DISCHARGE,
	ST_CHARGE,
	ST_PRECHARGE,

	CHARGE_STATE_COUNT
};

struct charge_state_data {
	timestamp_t ts;
	int ac;
	int batt_is_charging;
	struct charger_params chg;
	struct batt_params batt;
	enum charge_state state;
	int requested_voltage;
	int requested_current;
	int desired_input_current;
#ifdef CONFIG_CHARGER_OTG
	int output_current;
#endif
#ifdef CONFIG_EC_EC_COMM_BATTERY_CLIENT
	int input_voltage;
#endif
#ifdef CONFIG_OCPC
	struct ocpc_data ocpc;
#endif
};

struct sustain_soc {
	int8_t lower;
	int8_t upper;
	uint8_t flags; /* enum ec_charge_control_flag */
};

#define BAT_MAX_DISCHG_CURRENT 5000 /* mA */
#define BAT_LOW_VOLTAGE_THRESH 3200 /* mV */

/**
 * Return current charge state for LED control.
 */
enum led_pwr_state led_pwr_get_state(void);

/**
 * Return current charge task state.
 */
__test_only enum charge_state charge_get_state(void);

/**
 * Return non-zero if battery is so low we want to keep AP off.
 */
int charge_keep_power_off(void);

/**
 * Return current charge LED state flags (CHARGE_LED_FLAG_*)
 *
 * This API is being deprecated. It has been used only for LED control and is
 * being replaced by led_pwr_get_state.
 */
#if defined(BOARD_ELM) || defined(BOARD_REEF_MCHP) || defined(TEST_BUILD)
uint32_t charge_get_led_flags(void);
#endif

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
 * @brief whether or not the charge state will prevent power-on
 *
 * @param power_button_pressed	True if the power-up attempt is caused by a
 *				power button press.
 * @return True if the battery level is too low to allow power on, even if a
 *         charger is attached.
 */
#ifdef CONFIG_BATTERY
bool charge_prevent_power_on(bool power_button_pressed);
#else
static inline bool charge_prevent_power_on(bool power_button_pressed)
{
	return false;
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

/**
 * Set the output current limit and voltage. This is used to provide power from
 * the charger chip ("OTG" mode).
 *
 * @param chgnum Charger index to act upon
 * @param ma Maximum current to provide in mA (0 to disable output).
 * @param mv Voltage in mV (ignored if ma == 0).
 * @return EC_SUCCESS or error
 */
int charge_set_output_current_limit(int chgnum, int ma, int mv);

/**
 * Set the charge input current limit. This value is stored and sent every
 * time AC is applied.
 *
 * The input current limit is automatically derated by
 * CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT (if configured), and is clamped to
 * no less than CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT mA (if configured).
 *
 * @param ma New input current limit in mA
 * @param mv Negotiated charge voltage in mV.
 * @return EC_SUCCESS or error
 */
int charge_set_input_current_limit(int ma, int mv);

/**
 * Set the desired manual charge current when in idle mode.
 *
 * @param curr_ma: Charge current in mA.
 */
void chgstate_set_manual_current(int curr_ma);

/**
 * Set the desired manual charge voltage when in idle mode.
 *
 * @param volt_mv: Charge voltage in mV.
 */
void chgstate_set_manual_voltage(int volt_mv);

/**
 * Board-specific routine to indicate if the base is connected.
 */
int board_is_base_connected(void);

/**
 * Board-specific routine to enable power distribution between lid and base
 * (current can flow both ways).
 */
void board_enable_base_power(int enable);

/**
 * Board-specific routine to reset the base (in case it is unresponsive, e.g.
 * if we told it to hibernate).
 */
void board_base_reset(void);

/**
 * Callback with which boards determine action on critical low battery
 *
 * The default implementation is provided in charge_state.c. Overwrite it
 * to customize it.
 *
 * @param curr Pointer to struct charge_state_data
 * @return Action to take.
 */
enum critical_shutdown
board_critical_shutdown_check(struct charge_state_data *curr);

/**
 * Callback to set battery level for shutdown
 *
 * A board can implement this to customize shutdown battery level at runtime.
 *
 * @return battery level for shutdown
 */
uint8_t board_set_battery_level_shutdown(void);

/**
 * Select which charger IC will actually be performing the charger switching.
 *
 * @param idx The index into the chg_chips table.
 */
void charge_set_active_chg_chip(int idx);

/**
 * Retrieve which charger IC is the active charger IC performing the charger
 * switching.
 */
int charge_get_active_chg_chip(void);

/**
 * Reset the OCPC internal state data and set the target VSYS to the current
 * battery voltage for the auxiliary chargers.
 */
void trigger_ocpc_reset(void);

/* Track problems in communicating with the battery or charger */
enum problem_type {
	PR_STATIC_UPDATE,
	PR_SET_VOLTAGE,
	PR_SET_CURRENT,
	PR_SET_MODE,
	PR_SET_INPUT_CURR,
	PR_POST_INIT,
	PR_CHG_FLAGS,
	PR_BATT_FLAGS,
	PR_CUSTOM,
	PR_CFG_SEC_CHG,

	NUM_PROBLEM_TYPES
};

void charge_problem(enum problem_type p, int v);

struct charge_state_data *charge_get_status(void);

enum ec_charge_control_mode get_chg_ctrl_mode(void);

__test_only void reset_prev_disp_charge(void);

/**
 * Whether or not the charging progress was shown. Note, calling this function
 * will reset the value to false.
 *
 * @return Whether or not the charging progress was printed to the console
 */
__test_only bool charging_progress_displayed(void);

/**
 * Callback for boards to request charger to enable bypass mode on/off.
 *
 * @return True for requesting bypass on. False for requesting bypass off.
 */
int board_should_charger_bypass(void);

#ifdef __cplusplus
}
#endif

/* Config Charger */
#include "charge_state.h"

#endif /* __CROS_EC_CHARGE_STATE_H */
