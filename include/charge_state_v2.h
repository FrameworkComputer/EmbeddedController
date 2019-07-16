/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "chipset.h"
#include "ec_ec_comm_master.h"
#include "timer.h"

#ifndef __CROS_EC_CHARGE_STATE_V2_H
#define __CROS_EC_CHARGE_STATE_V2_H

#if defined(CONFIG_I2C_VIRTUAL_BATTERY) && defined(CONFIG_BATTERY_SMART)
#define VIRTUAL_BATTERY_ADDR_FLAGS BATTERY_ADDR_FLAGS
#endif
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
#ifdef CONFIG_EC_EC_COMM_BATTERY_MASTER
	int input_voltage;
#endif
};

/**
 * Set the output current limit and voltage. This is used to provide power from
 * the charger chip ("OTG" mode).
 *
 * @param ma Maximum current to provide in mA (0 to disable output).
 * @param mv Voltage in mV (ignored if ma == 0).
 * @return EC_SUCCESS or error
 */
int charge_set_output_current_limit(int ma, int mv);

/**
 * Set the charge input current limit. This value is stored and sent every
 * time AC is applied.
 *
 * @param ma New input current limit in mA
 * @param mv Negotiated charge voltage in mV.
 * @return EC_SUCCESS or error
 */
int charge_set_input_current_limit(int ma, int mv);

/*
 * Expose charge/battery related state
 *
 * @param param command to get corresponding data
 * @param value the corresponding data
 * @return EC_SUCCESS or error
 */
#ifdef CONFIG_CHARGE_STATE_DEBUG
int charge_get_charge_state_debug(int param, uint32_t *value);
#endif /* CONFIG_CHARGE_STATE_DEBUG */

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
enum critical_shutdown board_critical_shutdown_check(
		struct charge_state_data *curr);

/**
 * Callback to set battery level for shutdown
 *
 * A board can implement this to customize shutdown battery level at runtime.
 *
 * @return battery level for shutdown
 */
uint8_t board_set_battery_level_shutdown(void);

#endif /* __CROS_EC_CHARGE_STATE_V2_H */
