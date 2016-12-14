/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Charger profile override for fast charging
 */

#ifndef __CROS_EC_CHARGER_PROFILE_OVERRIDE_H
#define __CROS_EC_CHARGER_PROFILE_OVERRIDE_H

#include "charge_state_v2.h"

#define TEMPC_TENTHS_OF_DEG(c) ((c) * 10)

#define CHARGER_PROF_TEMP_C_LAST_RANGE 0xFFFF

enum fast_chg_voltage_ranges {
	VOLTAGE_RANGE_LOW,
	VOLTAGE_RANGE_HIGH,
	VOLTAGE_RANGE_NUM,
};

/* Charge profile override info */
struct fast_charge_profile {
	/* temperature in 10ths of a degree C */
	int temp_c;
	/* charge current at lower & higher battery voltage limit in mA */
	int current_mA[VOLTAGE_RANGE_NUM];
};

/* Charge profile override parameters */
struct fast_charge_params {
	/* Total temperature ranges of the charge profile */
	const int total_temp_ranges;
	/* Default temperature range of the charge profile */
	const int default_temp_range_profile;
	/*
	 * Lower limit of battery voltage in mV
	 * If the battery voltage reading is bad or the battery voltage is
	 * greater than or equal to the lower limit or the battery voltage is
	 * not in the charger profile voltage range, consider battery has high
	 * voltage range so that we charge at lower current limit.
	 */
	const int vtg_low_limit_mV;
	const struct fast_charge_profile *chg_profile_info;
};

/**
 * Optional customization of charger profile override for fast charging.
 *
 * On input, the struct reflects the default behavior. The function can make
 * changes to the state, requested_voltage, or requested_current.
 *
 * @param curr Charge state machine data.
 *
 * @return
 *   >0    Desired time in usec for this poll period.
 *   0     Use the default poll period (which varies with the state).
 *  <0     An error occurred. The poll time will be shorter than usual.
 *         Too many errors in a row may trigger some corrective action.
 */
int charger_profile_override(struct charge_state_data *curr);

/**
 * Common code of charger profile override for fast charging.
 *
 * @param curr               Charge state machine data.
 * @param fast_chg_params    Fast charge profile parameters.
 * @param prev_chg_prof_info Previous charge profile info.
 * @param batt_vtg_max       Maximum battery voltage.
 *
 * @return
 *   >0    Desired time in usec for this poll period.
 *   0     Use the default poll period (which varies with the state).
 *  <0     An error occurred. The poll time will be shorter than usual.
 *         Too many errors in a row may trigger some corrective action.
 */
int charger_profile_override_common(struct charge_state_data *curr,
			const struct fast_charge_params *fast_chg_params,
			const struct fast_charge_profile **prev_chg_prof_info,
			int batt_vtg_max);

/*
 * Access to custom profile params through host commands.
 * What this does is up to the implementation.
 */
enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value);
enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value);

#endif /* __CROS_EC_CHARGER_PROFILE_OVERRIDE_H */
