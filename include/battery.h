/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging parameters and constraints
 */

#ifndef __CROS_EC_BATTERY_H
#define __CROS_EC_BATTERY_H

#include "common.h"

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

/*
 * Sometimes we have hardware to detect battery present, sometimes we have to
 * wait until we've been able to talk to the battery.
 */
enum battery_present {
	BP_NO = 0,
	BP_YES = 1,
	BP_NOT_SURE,
};

/* Battery parameters */
struct batt_params {
	int temperature;      /* Temperature in 0.1 K */
	int state_of_charge;  /* State of charge (percent, 0-100) */
	int voltage;          /* Battery voltage (mV) */
	int current;          /* Battery current (mA); negative=discharging */
	int desired_voltage;  /* Charging voltage desired by battery (mV) */
	int desired_current;  /* Charging current desired by battery (mA) */
	int remaining_capacity;  /* Remaining capacity in mAh */
	int full_capacity;    /* Capacity in mAh (might change occasionally) */
	enum battery_present is_present; /* Is the battery physically present */
	int flags;            /* Flags */
};

/* Flags for batt_params */

/* Battery wants to be charged */
#define BATT_FLAG_WANT_CHARGE			0x00000001

/* Battery is responsive (talking to us via I2C) */
#define BATT_FLAG_RESPONSIVE			0x00000002

/* Bits to indicate which parameter(s) could not be read */
#define BATT_FLAG_BAD_TEMPERATURE		0x00000004
#define BATT_FLAG_BAD_STATE_OF_CHARGE		0x00000008
#define BATT_FLAG_BAD_VOLTAGE			0x00000010
#define BATT_FLAG_BAD_CURRENT			0x00000020
#define BATT_FLAG_BAD_DESIRED_VOLTAGE		0x00000040
#define BATT_FLAG_BAD_DESIRED_CURRENT		0x00000080
#define BATT_FLAG_BAD_REMAINING_CAPACITY	0x00000100
#define BATT_FLAG_BAD_FULL_CAPACITY		0x00000200
/* All of the above BATT_FLAG_BAD_* bits */
#define BATT_FLAG_BAD_ANY			0x000003fc

/* Battery constants */
struct battery_info {
	/* Design voltage in mV */
	int voltage_max;
	int voltage_normal;
	int voltage_min;
	/* Pre-charge current in mA */
	int precharge_current;
	/* Working temperature ranges in degrees C */
	int8_t start_charging_min_c;
	int8_t start_charging_max_c;
	int8_t charging_min_c;
	int8_t charging_max_c;
	int8_t discharging_min_c;
	int8_t discharging_max_c;
};

/**
 * Return vendor-provided battery constants.
 */
const struct battery_info *battery_get_info(void);

/**
 * Get current battery parameters.
 *
 * Error conditions are reported via batt.flags.
 *
 * @param batt		Destination for battery data
 */
void battery_get_params(struct batt_params *batt);

/**
 * Modify battery parameters to match vendor charging profile.
 *
 * @param batt		Battery parameters to modify
 */
void battery_vendor_params(struct batt_params *batt);

/**
 * Check for presence of battery.
 *
 * @return Whether there is a battery attached or not, or if we can't tell.
 */
enum battery_present battery_is_present(void);

/**
 * Get battery mode.
 *
 * See MODE_* constants in battery_smart.h
 *
 * @param mode         Destination for current mode.
 * @return non-zero if error.
 */
int battery_get_mode(int *mode);

/**
 * Read nominal voltage battery is designed to supply.
 *
 * @param voltage	Destination for voltage in mW
 * @return non-zero if error.
 */
int battery_design_voltage(int *voltage);

/**
 * Read absolute state of charge.
 *
 * @param percent	Destination for charge in percent
 * @return non-zero if error.
 */
int battery_state_of_charge_abs(int *percent);

/**
 * Read battery remaining capacity.
 *
 * @param capacity	Destination for capacity in mAh
 * @return non-zero if error.
 */
int battery_remaining_capacity(int *capacity);

/**
 * Read battery full charge capacity.
 *
 * @param capacity	Destination for capacity in mAh
 * @return non-zero if error.
 */
int battery_full_charge_capacity(int *capacity);

/**
 * Read the nominal capacity the battery is designed to supply when new.
 *
 * @param capacity	Destination for capacity in mAh
 * @return non-zero if error.
 */
int battery_design_capacity(int *capacity);

/**
 * Read time in minutes left when discharging.
 *
 * @param capacity	Destination for remaining time in minutes.
 * @return non-zero if error.
 */
int battery_time_to_empty(int *minutes);

/**
 * Read run time in minutes left when discharging.
 *
 * @param capacity	Destination for remaining time in minutes.
 * @return non-zero if error.
 */
int battery_run_time_to_empty(int *minutes);

/**
 * Read time in minutes left to full capacity when charging.
 *
 * @param capacity	Destination for remaining time in minutes.
 * @return non-zero if error.
 */
int battery_time_to_full(int *minutes);

/**
 * Calculate battery time in minutes, under an assumed current.
 *
 * @param rate		Current to use for calculation, in mA.
 *			If > 0, calculates charging time; if < 0, calculates
 *			discharging time; 0 is invalid and sets minutes=0.
 * @param minutes	Destination for calculated time in minutes.
 * @return non-zero if error.
 */
int battery_time_at_rate(int rate, int *minutes);

/**
 * Read battery status.
 *
 * @param status	Destination for status; see STATUS_* in battery_smart.h.
 * @return non-zero if error.
 */
int battery_status(int *status);

/**
 * Read battery charge cycle count.
 *
 * @param count		Destination for count.
 * @return non-zero if error.
 */
int battery_cycle_count(int *count);

/**
 * Read battery serial number.
 *
 * @param serial	Destination for serial number.
 * @return non-zero if error.
 */
int battery_serial_number(int *serial);

/**
 * Read manufacturer name.
 *
 * @param dest		Destination buffer.
 * @param size		Length of destination buffer in chars.
 * @return non-zero if error.
 */
int battery_manufacturer_name(char *dest, int size);

/**
 * Read device name.
 *
 * @param dest		Destination buffer.
 * @param size		Length of destination buffer in chars.
 * @return non-zero if error.
 */
int battery_device_name(char *dest, int size);

/**
 * Read battery type/chemistry.
 *
 * @param dest		Destination buffer.
 * @param size		Length of destination buffer in chars.
 * @return non-zero if error.
 */
int battery_device_chemistry(char *dest, int size);

/**
 * Read device manufacture date.
 *
 * @param year		Destination for year
 * @param month		Destination for month
 * @param day		Destination for day
 * @return non-zero if error.
 */
int battery_manufacturer_date(int *year, int *month, int *day);

/**
 * Call board-specific cut-off function.
 *
 * @return EC_RES_INVALID_COMMAND if the battery doesn't support.
 */
int board_cut_off_battery(void);

/**
 * Return if the battery has been cut off.
 */
int battery_is_cut_off(void);

#endif /* __CROS_EC_BATTERY_H */
