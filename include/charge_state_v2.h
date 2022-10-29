/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "chipset.h"
#include "ec_ec_comm_client.h"
#include "ocpc.h"
#include "timer.h"

#ifndef __CROS_EC_CHARGE_STATE_V2_H
#define __CROS_EC_CHARGE_STATE_V2_H

/*
 * The values exported by charge_get_state() and charge_get_flags() are used
 * only to control the LEDs (with one not-quite-correct exception). For V2
 * we use a different set of states internally.
 */
enum charge_state_v2 {
	ST_IDLE = 0,
	ST_DISCHARGE,
	ST_CHARGE,
	ST_PRECHARGE,

	NUM_STATES_V2
};

struct charge_state_data {
	timestamp_t ts;
	int ac;
	int batt_is_charging;
	struct charger_params chg;
	struct batt_params batt;
	enum charge_state_v2 state;
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
};

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
 * The default implementation is provided in charge_state_v2.c. Overwrite it
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
 * Return system PLT power and battery's desired power.
 *
 * @return desired power in mW
 */
int charge_get_plt_plus_bat_desired_mw(void);

/**
 * Get the stable battery charging current. The current will be
 * CHARGE_CURRENT_UNINITIALIZED if not yet stable.
 *
 * @return stable battery charging current in mA
 */
int charge_get_stable_current(void);

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
 * Set the stable current.
 *
 * @param ma: battery charging current in mA
 */
void charge_set_stable_current(int ma);

/**
 * Reset stable current counter stable_ts. Calling this function would set
 * stable_current to CHARGE_CURRENT_UNINITIALIZED.
 */
void charge_reset_stable_current(void);

/**
 * Reset stable current counter stable_ts. Calling this function would set
 * stable_current to CHARGE_CURRENT_UNINITIALIZED.
 *
 * @param us: sample stable current until us later.
 */
void charge_reset_stable_current_us(uint64_t us);

/**
 * Check if the battery charging current is stable by examining the timestamp.
 *
 * @return true if stable timestamp expired, false otherwise.
 */
bool charge_is_current_stable(void);

int set_chg_ctrl_mode(enum ec_charge_control_mode mode);

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

#endif /* __CROS_EC_CHARGE_STATE_V2_H */
