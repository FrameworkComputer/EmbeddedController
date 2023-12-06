/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging parameters and constraints
 */

#ifndef __CROS_EC_BATTERY_H
#define __CROS_EC_BATTERY_H

#include "common.h"
#include "compiler.h"
#include "ec_commands.h"
#include "host_command.h"

/*
 * If compiling with Zephyr, include the BATTERY_LEVEL_ definitions that are
 * shared with device tree
 */
#ifdef CONFIG_ZEPHYR

#include "dt-bindings/battery.h"

#else /* !CONFIG_ZEPHYR */

/* Stop charge when charging and battery level >= this percentage */
#define BATTERY_LEVEL_FULL 100

/*
 * Send battery-low host event when discharging and battery level <= this level
 */
#define BATTERY_LEVEL_LOW 10

/*
 * Send battery-critical host event when discharging and battery level <= this
 * level.
 */
#define BATTERY_LEVEL_CRITICAL 5

/*
 * Shut down main processor and/or hibernate EC when discharging and battery
 * level < this level. Setting this too low makes the battery discharge too
 * deeply, which isn't good for the battery health.
 */
#define BATTERY_LEVEL_SHUTDOWN 3

#endif /* CONFIG_ZEPHYR */

/* Full-capacity change reqd for host event */
#define LFCC_EVENT_THRESH 5

/* Battery index, only used with CONFIG_BATTERY_V2. */
enum battery_index {
	BATT_IDX_INVALID = -1,
	BATT_IDX_MAIN = 0,
	BATT_IDX_BASE = 1,
};

/*
 * Sometimes we have hardware to detect battery present, sometimes we have to
 * wait until we've been able to talk to the battery.
 */
FORWARD_DECLARE_ENUM(battery_present){
	BP_NOT_INIT = -1,
	BP_NO = 0,
	BP_YES = 1,
	BP_NOT_SURE,
};

enum battery_cutoff_states {
	/* Cutoff is not started or scheduled. */
	BATTERY_CUTOFF_STATE_NORMAL = 0,
	/* Cutoff has begun but not completed. */
	BATTERY_CUTOFF_STATE_IN_PROGRESS,
	/*
	 * Cutoff has been completed. This state is effectively unused if AC is
	 * unplugged because the EC will brown out when cutoff completes.
	 */
	BATTERY_CUTOFF_STATE_CUT_OFF,
	/*
	 * Cutoff is scheduled but hasn't started. Cutoff is deferred or the EC
	 * is waiting for a shutdown.
	 */
	BATTERY_CUTOFF_STATE_SCHEDULED,
};

enum battery_disconnect_state {
	BATTERY_DISCONNECTED = 0,
	BATTERY_NOT_DISCONNECTED,
	BATTERY_DISCONNECT_ERROR,
};

struct battery_static_info {
	uint16_t design_capacity;
	uint16_t design_voltage;
	uint32_t cycle_count;
	/*
	 * TODO: The fields below should be renamed & re-typed:
	 * uint16_t serial[32];
	 * char manufacturer[32];
	 * char device_name[32];
	 * char chemistry[32];
	 */
	char manufacturer_ext[SBS_MAX_STR_OBJ_SIZE]; /* SB_MANUFACTURER_NAME */
	char model_ext[SBS_MAX_STR_OBJ_SIZE]; /* SB_DEVICE_NAME */
	char serial_ext[SBS_MAX_STR_OBJ_SIZE]; /* SB_SERIAL_NUMBER */
	char type_ext[SBS_MAX_STR_OBJ_SIZE]; /* SB_DEVICE_CHEMISTRY */
#ifdef CONFIG_BATTERY_VENDOR_PARAM
	uint8_t vendor_param[SBS_MAX_STR_OBJ_SIZE];
#endif
};

extern struct battery_static_info battery_static[];
extern struct ec_response_battery_dynamic_info battery_dynamic[];

/* Battery parameters */
struct batt_params {
	int temperature; /* Temperature in 0.1 K */
	int state_of_charge; /* State of charge (percent, 0-100) */
	int voltage; /* Battery voltage (mV) */
	int current; /* Battery current (mA); negative=discharging */
	int desired_voltage; /* Charging voltage desired by battery (mV) */
	int desired_current; /* Charging current desired by battery (mA) */
	int remaining_capacity; /* Remaining capacity in mAh */
	int full_capacity; /* Capacity in mAh (might change occasionally) */
	int display_charge; /* Display charge in 10ths of a % (1000=100.0%) */
	int status; /* Battery status */
	enum battery_present is_present; /* Is the battery physically present */
	int flags; /* Flags */
};

/*
 * Provide a 1 minute average of the current and voltage on the battery.
 * Does not check for flags or whether those values are bad readings.
 * See driver/battery/[your_driver].h/c for details on implementation and
 * how the average is calculated.
 */

int battery_get_avg_current(void); /* in mA */
int battery_get_avg_voltage(void); /* in mV */

/* Flags for batt_params */

/* Battery wants to be charged */
#define BATT_FLAG_WANT_CHARGE 0x00000001

/* Battery is responsive (talking to us via I2C) */
#define BATT_FLAG_RESPONSIVE 0x00000002

/* Bits to indicate which parameter(s) could not be read */
#define BATT_FLAG_BAD_TEMPERATURE 0x00000004
#define BATT_FLAG_BAD_STATE_OF_CHARGE 0x00000008
#define BATT_FLAG_BAD_VOLTAGE 0x00000010
#define BATT_FLAG_BAD_CURRENT 0x00000020
#define BATT_FLAG_BAD_DESIRED_VOLTAGE 0x00000040
#define BATT_FLAG_BAD_DESIRED_CURRENT 0x00000080
#define BATT_FLAG_BAD_REMAINING_CAPACITY 0x00000100
#define BATT_FLAG_BAD_FULL_CAPACITY 0x00000200
#define BATT_FLAG_BAD_STATUS 0x00000400
#define BATT_FLAG_IMBALANCED_CELL 0x00000800
#define BATT_FLAG_BAD_AVERAGE_CURRENT 0x00001000
/* All of the above BATT_FLAG_BAD_* bits */
#define BATT_FLAG_BAD_ANY 0x000017fc
/* Flags which are set or unset on every access (via battery_get_params) */
#define BATT_FLAG_VOLATILE                                                  \
	(BATT_FLAG_BAD_ANY | BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE | \
	 BATT_FLAG_IMBALANCED_CELL)

/* The flag of prechare when the battery voltage is lower than voltage_min */
#define BATT_FLAG_DEEP_CHARGE 0x00010000

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
void battery_override_params(struct batt_params *batt);

#if defined(CONFIG_BATTERY) || defined(CONFIG_BATTERY_PRESENT_CUSTOM)
/**
 * Check for presence of battery.
 *
 * @return Whether there is a battery attached or not, or if we can't tell.
 */
enum battery_present battery_is_present(void);
#else
/*
 * If battery support is not enabled and the board does not specifically
 * provide its own implementation, assume a battery is never present.
 */
test_mockable_static_inline enum battery_present battery_is_present(void)
{
	return BP_NO;
}
#endif

/**
 * Check for physical presence of battery.
 *
 * @return Whether there is a battery physically present, but possibly
 * in a disconnected or cut off state, or if we can't tell;
 */
enum battery_present battery_hw_present(void);

/**
 * Check for battery initialization status.
 *
 * @return zero if not initialized.
 */
int board_battery_initialized(void);

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
 * Read battery manufacture date.
 *
 * @param year		Destination for year
 * @param month		Destination for month
 * @param day		Destination for day
 * @return non-zero if error.
 */
int battery_manufacture_date(int *year, int *month, int *day);

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
 * Read manufacturer name.
 *
 * This can be overridden to return a chip or board custom string.
 *
 * @param dest		Destination buffer.
 * @param size		Length of destination buffer in chars.
 * @return non-zero if error.
 */
int get_battery_manufacturer_name(char *dest, int size);

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
 * Read battery manufacturer data.
 *
 * @param dest		Destination buffer.
 * @param size		Length of destination buffer.
 * @return non-zero if error.
 */
int battery_manufacturer_data(char *data, int size);

/**
 * Write battery manufacturer access.
 *
 * @param cmd		Destiation for battery manufacturer access command.
 * @retun non-zeor if error.
 */
int battery_manufacturer_access(int cmd);

/**
 * Report the absolute difference between the highest and lowest cell voltage in
 * the battery pack, in millivolts.  On error or unimplemented, returns '0'.
 */
int battery_imbalance_mv(void);
int battery_bq4050_imbalance_mv(void);

/**
 * Call board-specific cut-off function.
 *
 * @return EC_RES_INVALID_COMMAND if the battery doesn't support.
 */
int board_cut_off_battery(void);

/**
 * Return if the battery start cut off.
 */
int battery_cutoff_in_progress(void);

/**
 * Return if the battery has been cut off.
 */
int battery_is_cut_off(void);

/**
 * Read battery vendor parameter.
 *
 * Vendor parameter handlers are implemented in a board-specific battery.c
 *
 * @param param		Parameter identifier.
 * @param value		Location to store retrieved value.
 * @return non-zero if error.
 */
__override_proto int battery_get_vendor_param(uint32_t param, uint32_t *value);

/**
 * Write battery vendor parameter.
 *
 * Vendor parameter handlers are implemented in a board-specific battery.c
 *
 * @param param		Parameter identifier.
 * @param value		Value to write to the battery.
 * @return non-zero if error.
 */
__override_proto int battery_set_vendor_param(uint32_t param, uint32_t value);

/**
 * Wait for battery stable.
 *
 * @return non-zero if error.
 */
int battery_wait_for_stable(void);

/**
 * Print all battery info for debugging purposes
 */
void print_battery_debug(void);

/**
 * Get the disconnect state of the battery.
 */
enum battery_disconnect_state battery_get_disconnect_state(void);

#ifdef CONFIG_BATTERY_V2
/**
 * Refresh battery information in host memory mapped region, if index is
 * currently presented.
 */
void battery_memmap_refresh(enum battery_index index);

/**
 * Set which index to present in host memory mapped region.
 */
void battery_memmap_set_index(enum battery_index index);
#endif /* CONFIG_BATTERY_V2 */

#ifdef CONFIG_CMD_I2C_STRESS_TEST_BATTERY
extern struct i2c_stress_test_dev battery_i2c_stress_test_dev;
#endif

/*
 * If remaining charge is more than x% of the full capacity, the
 * remaining charge is raised to the full capacity before it's
 * reported to the rest of the system.
 *
 * Some batteries don't update full capacity timely or don't update it
 * at all. On such systems, compensation is required to guarantee
 * the remaining charge will be equal to the full capacity eventually.
 *
 * On some systems, Rohm charger generates audio noise when the battery
 * is fully charged and AC is plugged. A workaround is to do charge-
 * discharge cycles between 93 and 100%. On such systems, compensation
 * was also applied to mask this cycle from users.
 *
 * This used to be done in ACPI, thus, all software components except EC
 * was seeing the compensated charge. Now we do it in EC. It has more
 * knowledge on the charger and the battery. So, it can perform more
 * granular and precise compensation.
 *
 * TODO: Currently, this is applied only to smart battery. Apply it to other
 *       battery drivers as needed.
 */
void battery_compensate_params(struct batt_params *batt);

/**
 * board-specific battery_compensate_params
 */
__override_proto void board_battery_compensate_params(struct batt_params *batt);

void battery_validate_params(struct batt_params *batt);

/**
 * Read static battery info from a main battery and store it in a cache.
 *
 * @return EC_SUCCESS or EC_ERROR_*.
 */
int update_static_battery_info(void);

/**
 * Read dynamic battery info from a main battery and store it in a cache.
 */
void update_dynamic_battery_info(void);

#endif /* __CROS_EC_BATTERY_H */
