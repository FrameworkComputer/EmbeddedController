/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charger/battery debug command module for Chrome EC */

#ifndef __CROS_EC_CHARGER_H
#define __CROS_EC_CHARGER_H

#include "common.h"

/* Charger infomation
 * voltage unit: mV
 * current unit: mA
 */
struct charger_info {
	const char *name;
	uint16_t voltage_max;
	uint16_t voltage_min;
	uint16_t voltage_step;
	uint16_t current_max;
	uint16_t current_min;
	uint16_t current_step;
	uint16_t input_current_max;
	uint16_t input_current_min;
	uint16_t input_current_step;
};

/* Power state machine post init */
int charger_post_init(void);

/* Get charger information. */
const struct charger_info *charger_get_info(void);

/* Get smart battery charger status. Supported flags:
 *     CHARGER_CHARGE_INHIBITED
 *     CHARGER_LEVEL_2
 */
int charger_get_status(int *status);

/* Set smart battery charger mode. Supported mode(s):
 *     CHARGER_FLAG_INHIBIT_CHARGE
 */
int charger_set_mode(int mode);

/**
 * Return the closest match the charger can supply to the requested current.
 *
 * @param current	Requested current in mA.
 *
 * @return Current the charger will actually supply if <current> is requested.
 */
int charger_closest_current(int current);

/* Get/set charge current limit in mA */
int charger_get_current(int *current);
int charger_set_current(int current);

/* Get/set charge voltage limit in mV */
int charger_get_voltage(int *voltage);
int charger_set_voltage(int voltage);

#endif /* __CROS_EC_CHARGER_H */

