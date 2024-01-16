/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Charger profile override for fast charging
 */

#ifndef __CROS_EC_CHARGER_PROFILE_OVERRIDE_H
#define __CROS_EC_CHARGER_PROFILE_OVERRIDE_H

#include "charge_state.h"

#define TEMPC_TENTHS_OF_DEG(c) ((c) * 10)

#define CHARGER_PROF_TEMP_C_LAST_RANGE 0xFFFF

#define CHARGER_PROF_VOLTAGE_MV_LAST_RANGE 0xFFFF

/* Charge profile override info */
struct fast_charge_profile {
	/* temperature in 10ths of a degree C */
	const int temp_c;
	/* charge current for respective battery voltage ranges in mA. */
	const int current_mA[CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES];
};

/* Charge profile override parameters */
struct fast_charge_params {
	/* Total temperature ranges of the charge profile */
	const int total_temp_ranges;
	/* Default temperature range of the charge profile */
	const int default_temp_range_profile;
	/*
	 * Battery voltage ranges in mV.
	 * It is assumed that these values are added in ascending order in the
	 * board battery file.
	 */
	const int voltage_mV[CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES];
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
int charger_profile_override_common(
	struct charge_state_data *curr,
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
