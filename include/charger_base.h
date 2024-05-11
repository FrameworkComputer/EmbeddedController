/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charger functions related to a connected keyboard called a 'base' */

#ifndef __CROS_EC_CHARGER_BASE_H
#define __CROS_EC_CHARGER_BASE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct charge_state_data;

/* allocate power between the base and the lid */
void base_charge_allocate_input_current_limit(
	const struct charge_state_data *curr, bool is_full, bool debugging);

/*
 * Check base external-power settings and react as needed
 *
 * @param ac Current ac value from struct charge_state_data
 * @param prev_ac Previous value of ac
 * Returns true if ac should be zeroed, false to leave it along
 */
bool base_check_extpower(int ac, int prev_ac);

/* Update base battery information */
void base_update_battery_info(void);

#ifdef CONFIG_EC_EC_COMM_BATTERY_CLIENT
/* Check if there is a base and it is connected */
bool base_connected(void);

#else
static inline bool base_connected(void)
{
	return false;
}
#endif

/* Set up the charger task for the base */
void charger_base_setup(void);

/* Check if charge_base has changed since last time */
bool charger_base_charge_changed(void);

/* Update prev_charge_base with charge_base */
void charger_base_charge_update(void);

/* Show the current charge level of the base on the console */
void charger_base_show_charge(void);

/* Check if the base charge is near full */
bool charger_base_charge_near_full(void);

/* Get the base input-voltage */
int charger_base_get_input_voltage(const struct charge_state_data *curr);

/* Set the input voltage for the base */
void charger_base_set_input_voltage(struct charge_state_data *curr,
				    int input_voltage);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CHARGER_BASE_H */
