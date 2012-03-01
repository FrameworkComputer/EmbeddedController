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

/* Vendor provided parameters for battery charging */
void battery_vendor_params(struct batt_params *batt);

#endif //__CROS_EC_BATTERY_PACK_H

