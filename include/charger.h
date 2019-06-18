/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charger/battery debug command module for Chrome EC */

#ifndef __CROS_EC_CHARGER_H
#define __CROS_EC_CHARGER_H

#include "common.h"

/* Charger information
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

/*
 * Parameters common to all chargers. Current is in mA, voltage in mV.
 * The status and option values are charger-specific.
 */
struct charger_params {
	int current;
	int voltage;
	int input_current;
	int status;
	int option;
	int flags;
};

/* Get the current charger_params. Failures are reported in .flags */
void charger_get_params(struct charger_params *chg);

/* Bits to indicate which fields of struct charger_params could not be read */
#define CHG_FLAG_BAD_CURRENT		0x00000001
#define CHG_FLAG_BAD_VOLTAGE		0x00000002
#define CHG_FLAG_BAD_INPUT_CURRENT	0x00000004
#define CHG_FLAG_BAD_STATUS		0x00000008
#define CHG_FLAG_BAD_OPTION		0x00000010
/* All of the above CHG_FLAG_BAD_* bits */
#define CHG_FLAG_BAD_ANY                0x0000001f

/* Power state machine post init */
int charger_post_init(void);

/* Get charger information. */
const struct charger_info *charger_get_info(void);

/* Get smart battery charger status. Supported flags may vary. */
int charger_get_status(int *status);

/* Set smart battery charger mode. Supported modes may vary. */
int charger_set_mode(int mode);

/**
 * For chargers that are able to supply output power for OTG dongle, this
 * function enables or disables power output.
 */
int charger_enable_otg_power(int enabled);

/**
 * Sets OTG current limit and voltage (independent of whether OTG power is
 * currently enabled).
 *
 * Depending on the charger and use case, one needs to be careful about
 * changing the current/voltage while OTG power is enabled, and it might be wise
 * to reset the value before enabling OTG power to ensure one does not provide
 * excessive voltage to a device.
 *
 * @param output_current	Requested current limit in mA, driver should
 *                              round the value up.
 * @param output_voltage	Requested voltage in mV, driver should round the
 *                              the value down.
 *
 * @return EC_SUCCESS on success, an error otherwise.
 */
int charger_set_otg_current_voltage(int output_current, int output_voltage);

/**
 * Is the charger sourcing VBUS / OTG power?
 *
 * @param port The Type-C port number.
 * @return 1 if sourcing VBUS, 0 if not.
 */
int charger_is_sourcing_otg_power(int port);

/**
 * Return the closest match the charger can supply to the requested current.
 *
 * @param current	Requested current in mA.
 *
 * @return Current the charger will actually supply if <current> is requested.
 */
int charger_closest_current(int current);

/**
 * Return the closest match the charger can supply to the requested voltage.
 *
 * @param voltage	Requested voltage in mV.
 *
 * @return Voltage the charger will actually supply if <voltage> is requested.
 */
int charger_closest_voltage(int voltage);

/* Get/set charge current limit in mA */
int charger_get_current(int *current);
int charger_set_current(int current);

/* Get/set charge voltage limit in mV */
int charger_get_voltage(int *voltage);
int charger_set_voltage(int voltage);

/* Discharge battery when on AC power. */
int charger_discharge_on_ac(int enable);

/* Get the VBUS voltage (mV) from the charger */
int charger_get_vbus_voltage(int port);

/* Custom board function to discharge battery when on AC power */
int board_discharge_on_ac(int enable);

/*
 * Read the current total system power in uW (usually from PSYS).
 * Can be negative if the PSYS output is not currently enabled (e.g. AP is off).
 */
int charger_get_system_power(void);

/* Other parameters that may be charger-specific, but are common so far. */

/* Set desired input current value */
int charger_set_input_current(int input_current);

/*
 * Get actual input current value.
 * Actual input current may be less than the desired input current set
 * due to current ratings of the wall adapter.
 */
int charger_get_input_current(int *input_current);

int charger_manufacturer_id(int *id);
int charger_device_id(int *id);
int charger_get_option(int *option);
int charger_set_option(int option);
int charger_set_hw_ramp(int enable);

/* Print all charger info for debugging purposes */
void print_charger_debug(void);

#endif /* __CROS_EC_CHARGER_H */

