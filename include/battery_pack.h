/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common API for battery pack vendor provided charging profile
 */
#ifndef __CROS_EC_BATTERY_PACK_H
#define __CROS_EC_BATTERY_PACK_H

/* Battery parameters */
struct batt_params {
	int temperature;
	int state_of_charge;
	int voltage;
	int current;
	int desired_voltage;
	int desired_current;
};

/* Battery constants */
struct battery_info {
	/* Design voltage */
	int voltage_max;
	int voltage_normal;
	int voltage_min;
	/* Working temperature */
	int temp_charge_min;
	int temp_charge_max;
	int temp_discharge_min;
	int temp_discharge_max;
	/* Pre-charge */
	int precharge_current;
};

/* Vendor provided battery constants */
const struct battery_info *battery_get_info(void);

/* Vendor provided parameters for battery charging */
void battery_vendor_params(struct batt_params *batt);

#endif
