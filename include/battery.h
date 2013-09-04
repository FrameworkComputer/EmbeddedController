/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging parameters and constraints
 */

#ifndef __CROS_EC_BATTERY_H
#define __CROS_EC_BATTERY_H

/* Stop charge when charging and battery level >= this percentage */
#define BATTERY_LEVEL_FULL		100

/* Tell host we're charged when battery level >= this percentage */
#define BATTERY_LEVEL_NEAR_FULL		 97

/*
 * Send battery-low host event when discharging and battery level <= this level
 */
#define BATTERY_LEVEL_LOW		 10

/*
 * Send battery-critical host event when discharging and battery level <= this
 * level.
 */
#define BATTERY_LEVEL_CRITICAL		  5

/*
 * Shut down main processor and/or hibernate EC when discharging and battery
 * level < this level.
 */
#define BATTERY_LEVEL_SHUTDOWN		  3


/* Get/set battery mode */
int battery_get_battery_mode(int *mode);

int battery_set_battery_mode(int mode);

/* Read battery temperature
 * unit: 0.1 K
 */
int battery_temperature(int *deci_kelvin);

/* Read battery voltage
 * unit: mV
 */
int battery_voltage(int *voltage);

/* Relative state of charge in percent */
int battery_state_of_charge(int *percent);

/* Absolute state of charge in percent */
int battery_state_of_charge_abs(int *percent);

/*
 * Set 'val' to non-zero if the battery is reporting capacity in 10mW.
 * Otherwise, in mAh.
 */
int battery_is_in_10mw_mode(int *val);

/* Set battery capacity mode to mAh(=0) or 10mW(=1). */
int battery_set_10mw_mode(int enabled);

/*
 * Battery remaining capacity
 * unit: mAh or 10mW, depends on battery mode
 */
int battery_remaining_capacity(int *capacity);

/* Battery full charge capacity */
int battery_full_charge_capacity(int *capacity);

/* Time in minutes left when discharging */
int battery_time_to_empty(int *minutes);

int battery_run_time_to_empty(int *minutes);

/* Time in minutes to full when charging */
int battery_time_to_full(int *minutes);

/* The current battery desired to charge
 * unit: mA
 */
int battery_desired_current(int *current);

/* The voltage battery desired to charge
 * unit: mV
 */
int battery_desired_voltage(int *voltage);

/* Check if battery allows charging */
int battery_charging_allowed(int *allowed);

/* Read battery status */
int battery_status(int *status);

/* Battery charge cycle count */
int battery_cycle_count(int *count);

/* Designed battery capacity
 * unit: mAh or 10mW depends on battery mode
 */
int battery_design_capacity(int *capacity);

/* Designed battery output voltage
 * unit: mV
 */
int battery_design_voltage(int *voltage);

/* Read serial number */
int battery_serial_number(int *serial);

/* Read manufacturer name */
int battery_manufacturer_name(char *manufacturer_name, int buf_size);

/* Read device name */
int battery_device_name(char *device_name, int buf_size);

/* Read battery type/chemistry */
int battery_device_chemistry(char *device_chemistry, int buf_size);

/* Read battery discharging current
 * unit: mA
 * negative value: charging
 */
int battery_current(int *current);
int battery_average_current(int *current);

/* Calculate battery time in minutes, under a charging rate
 * rate >  0: charging, negative time to full
 * rate <  0: discharging, positive time to empty
 * rate == 0: invalid input, time = 0
 */
int battery_time_at_rate(int rate, int *minutes);

/* Read manufacturer date */
int battery_manufacturer_date(int *year, int *month, int *day);

#endif /* __CROS_EC_BATTERY_H */

