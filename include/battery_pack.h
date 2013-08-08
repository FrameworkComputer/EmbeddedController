/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common API for battery pack vendor provided charging profile
 */
#ifndef __CROS_EC_BATTERY_PACK_H
#define __CROS_EC_BATTERY_PACK_H

#include "common.h"

/* Battery parameters */
struct batt_params {
	int temperature;      /* Temperature in 0.1 K */
	int state_of_charge;  /* State of charge (percent, 0-100) */
	int voltage;          /* Battery voltage (mV) */
	int current;          /* Battery current (mA) */
	int desired_voltage;  /* Charging voltage desired by battery (mV) */
	int desired_current;  /* Charging current desired by battery (mA) */
};

/* Working temperature ranges in degrees C */
struct battery_temperature_ranges {
	int8_t start_charging_min_c;
	int8_t start_charging_max_c;
	int8_t charging_min_c;
	int8_t charging_max_c;
	int8_t discharging_min_c;
	int8_t discharging_max_c;
};
extern const struct battery_temperature_ranges bat_temp_ranges;

/* Battery constants */
struct battery_info {
	/* Design voltage in mV */
	int voltage_max;
	int voltage_normal;
	int voltage_min;
	/* Pre-charge current in mA */
	int precharge_current;
};

/**
 * Return vendor-provided battery constants.
 */
const struct battery_info *battery_get_info(void);

#ifdef CONFIG_BATTERY_VENDOR_PARAMS
/**
 * Modify battery parameters to match vendor charging profile.
 *
 * @param batt		Battery parameters to modify
 */
void battery_vendor_params(struct batt_params *batt);
#endif

#ifdef CONFIG_BATTERY_CHECK_CONNECTED
/**
 * Attempt communication with the battery.
 *
 * @return non-zero if the battery responds.
 */
int battery_is_connected(void);
#endif /* CONFIG_BATTERY_CHECK_CONNECTED */

#endif /* __CROS_EC_BATTERY_PACK_H */
