/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charger/battery debug command module for Chrome EC */

#ifndef __CROS_EC_CHARGER_H
#define __CROS_EC_CHARGER_H

#include "common.h"
#include "ocpc.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

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

struct charger_drv {
	/* Function to call during HOOK_INIT after i2c init */
	void (*init)(int chgnum);

	/* Power state machine post init */
	enum ec_error_list (*post_init)(int chgnum);

	/* Get charger information */
	const struct charger_info *(*get_info)(int chgnum);

	/* Get smart battery charger status. Supported flags may vary. */
	enum ec_error_list (*get_status)(int chgnum, int *status);

	/* Set smart battery charger mode. Supported modes may vary. */
	enum ec_error_list (*set_mode)(int chgnum, int mode);

	/*
	 * For chargers that are able to supply output power for OTG dongle,
	 * this function enables or disables power output.
	 */
	enum ec_error_list (*enable_otg_power)(int chgnum, int enabled);

	/*
	 * Sets OTG current limit and voltage (independent of whether OTG
	 * power is currently enabled).
	 */
	enum ec_error_list (*set_otg_current_voltage)(int chgnum,
						      int output_current,
						      int output_voltage);

	/*
	 * Is the charger sourcing VBUS / OTG power?
	 */
	int (*is_sourcing_otg_power)(int chgnum, int port);

	/* Get/set charge current limit in mA */
	enum ec_error_list (*get_current)(int chgnum, int *current);
	enum ec_error_list (*set_current)(int chgnum, int current);

	/* Get/set charge voltage limit in mV */
	enum ec_error_list (*get_voltage)(int chgnum, int *voltage);
	enum ec_error_list (*set_voltage)(int chgnum, int voltage);

	/* Get the measured charge current and voltage in mA/mV */
	enum ec_error_list (*get_actual_current)(int chgnum, int *current);
	enum ec_error_list (*get_actual_voltage)(int chgnum, int *voltage);

	/* Discharge battery when on AC power. */
	enum ec_error_list (*discharge_on_ac)(int chgnum, int enable);

	/* Get the VBUS voltage (mV) from the charger */
	enum ec_error_list (*get_vbus_voltage)(int chgnum, int port,
					       int *voltage);

	/* Get the Vsys voltage (mV) from the charger */
	enum ec_error_list (*get_vsys_voltage)(int chgnum, int port,
					       int *voltage);

	/* Set desired input current value */
	enum ec_error_list (*set_input_current_limit)(int chgnum,
						      int input_current);

	/* Get input current limit */
	enum ec_error_list (*get_input_current_limit)(int chgnum,
						      int *input_current);

	/* Get actual input current value */
	enum ec_error_list (*get_input_current)(int chgnum, int *input_current);

	enum ec_error_list (*manufacturer_id)(int chgnum, int *id);
	enum ec_error_list (*device_id)(int chgnum, int *id);
	enum ec_error_list (*set_frequency)(int chgnum, int freq_khz);
	enum ec_error_list (*get_option)(int chgnum, int *option);
	enum ec_error_list (*set_option)(int chgnum, int option);

	/* Charge ramp functions */
	enum ec_error_list (*set_hw_ramp)(int chgnum, int enable);
	int (*ramp_is_stable)(int chgnum);
	int (*ramp_is_detected)(int chgnum);
	int (*ramp_get_current_limit)(int chgnum);

	/* OCPC functions */
	/*
	 * Some chargers can perform VSYS output compensation.  Configure the
	 * charger IC with the right parameters.
	 *
	 * Returns EC_ERROR_UNIMPLEMENTED if further action is required from the
	 * OCPC control loop (which is typical), EC_SUCCESS if no further action
	 * is required, or any other status on error.
	 */
	enum ec_error_list (*set_vsys_compensation)(int chgnum,
						    struct ocpc_data *o,
						    int current_ma,
						    int voltage_mv);

	/* Is the input current limit reached? */
	enum ec_error_list (*is_icl_reached)(int chgnum, bool *reached);

	/* Enable/disable linear charging */
	enum ec_error_list (*enable_linear_charge)(int chgnum, bool enable);

	/*
	 * Enable/disable bypass mode
	 *
	 * Callers are responsible for checking required conditions beforehand.
	 * (e.g supplier == CHARGE_SUPPLIER_DEDICATED, 20 V < input_voltage)
	 */
	enum ec_error_list (*enable_bypass_mode)(int chgnum, bool enable);

	/*
	 * Get the number of battery cells from charging mode set by sensing an
	 * external resistor
	 */
	enum ec_error_list (*get_battery_cells)(int chgnum, int *cells);

	/* Dumps charger registers */
	void (*dump_registers)(int chgnum);

	/* Dumps prochot status information */
	void (*dump_prochot)(int chgnum);
};

struct charger_config_t {
	int i2c_port;
	uint16_t i2c_addr_flags;
	const struct charger_drv *drv;
};

#ifndef CONFIG_CHARGER_RUNTIME_CONFIG
extern const struct charger_config_t chg_chips[];
#else
extern struct charger_config_t chg_chips[];
#endif

__override_proto uint8_t board_get_charger_chip_count(void);

#ifdef CONFIG_CHARGER_SINGLE_CHIP
/*
 * Note: CHARGER_SOLO should be used anywhere the charger index being called is
 * only valid for a single-chip system.  This will then generate build errors if
 * the callsite is compliled for a multi-chip system, which needs to re-evaluate
 * the charger index to act upon.
 */
enum chg_id {
	CHARGER_SOLO,
	CHARGER_NUM,
};

#endif

/* Get the current charger_params. Failures are reported in .flags */
void charger_get_params(struct charger_params *chg);

/* Bits to indicate which fields of struct charger_params could not be read */
#define CHG_FLAG_BAD_CURRENT 0x00000001
#define CHG_FLAG_BAD_VOLTAGE 0x00000002
#define CHG_FLAG_BAD_INPUT_CURRENT 0x00000004
#define CHG_FLAG_BAD_STATUS 0x00000008
#define CHG_FLAG_BAD_OPTION 0x00000010
/* All of the above CHG_FLAG_BAD_* bits */
#define CHG_FLAG_BAD_ANY 0x0000001f

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

/* Driver wrapper functions */

/*
 * TODO(b/147672225): charger_* functions should take chip arg, hardcode
 * calls into driver to 0 for now.
 */

/* Power state machine post init */
enum ec_error_list charger_post_init(void);

/* Get charger information. */
const struct charger_info *charger_get_info(void);

/* Get smart battery charger status. Supported flags may vary. */
enum ec_error_list charger_get_status(int *status);

/* Set smart battery charger mode. Supported modes may vary. */
enum ec_error_list charger_set_mode(int mode);

/**
 * For chargers that are able to supply output power for OTG dongle, this
 * function enables or disables power output.
 */
enum ec_error_list charger_enable_otg_power(int chgnum, int enabled);

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
enum ec_error_list charger_set_otg_current_voltage(int chgnum,
						   int output_current,
						   int output_voltage);

/**
 * Is the charger sourcing VBUS / OTG power?
 *
 * @param port The Type-C port number.
 * @return 1 if sourcing VBUS, 0 if not.
 */
int charger_is_sourcing_otg_power(int port);

/* Get/set charge current limit in mA */
enum ec_error_list charger_get_current(int chgnum, int *current);
enum ec_error_list charger_set_current(int chgnum, int current);

/* Get/set charge voltage limit in mV */
enum ec_error_list charger_get_voltage(int chgnum, int *voltage);
enum ec_error_list charger_set_voltage(int chgnum, int voltage);

/* Get the measured charge current and voltage in mA/mV */
enum ec_error_list charger_get_actual_current(int chgnum, int *current);
enum ec_error_list charger_get_actual_voltage(int chgnum, int *voltage);

/* Discharge battery when on AC power. */
enum ec_error_list charger_discharge_on_ac(int enable);

/* Get the VBUS voltage (mV) from the charger */
enum ec_error_list charger_get_vbus_voltage(int port, int *voltage);

/* Get the Vsys voltage (mV) from the charger */
enum ec_error_list charger_get_vsys_voltage(int port, int *voltage);

/* Custom board function to discharge battery when on AC power */
int board_discharge_on_ac(int enable);

/*
 * Read the current total system power in uW (usually from PSYS).
 * Can be negative if the PSYS output is not currently enabled (e.g. AP is off).
 */
int charger_get_system_power(void);

/* Other parameters that may be charger-specific, but are common so far. */

/**
 * Set desired input current limit
 *
 * Sets the hard limit of the input current (from AC).
 *
 * @param chgnum		charger IC index
 * @param input_current		The current limit in mA.
 *
 * @return EC_SUCCESS on success, an error otherwise.
 */
enum ec_error_list charger_set_input_current_limit(int chgnum,
						   int input_current);

/**
 * Get desired input current limit
 *
 * Gets the hard limit of the input current (from AC).
 *
 * @param chgnum		charger IC index
 * @param input_current		The current limit in mA.
 *
 * @return EC_SUCCESS on success, an error otherwise.
 */
enum ec_error_list charger_get_input_current_limit(int chgnum,
						   int *input_current);

/**
 * Get actual input current value.
 *
 * Actual input current may be less than the desired input current set
 * due to current ratings of the wall adapter.
 *
 * @param chgnum		charger IC index
 * @param input_current		The input current in mA.
 *
 * @return EC_SUCCESS on success, an error otherwise.
 */
enum ec_error_list charger_get_input_current(int chgnum, int *input_current);

enum ec_error_list charger_manufacturer_id(int *id);
enum ec_error_list charger_device_id(int *id);
enum ec_error_list charger_get_option(int *option);
enum ec_error_list charger_set_frequency(int freq_khz);
enum ec_error_list charger_set_option(int option);
enum ec_error_list charger_set_hw_ramp(int enable);

/**
 * Some charger ICs can compensate for board losses if charging from an
 * auxiliary charger in a multi-charger IC design. (CONFIG_OCPC) Some of those
 * charger ICs can dynamically compensate meaning that the PID loop may not be
 * needed.  For the others, it still will be needed.  The charger driver should
 * return the appropriate action.
 *
 * @param chgnum: Active charge port
 * @param ocpc: Pointer to ocpc data
 * @param current_ma: Desired charge current
 * @param voltage_mv: Desired charge voltage
 * @return EC_SUCCESS on success, error otherwise.
 */
enum ec_error_list charger_set_vsys_compensation(int chgnum,
						 struct ocpc_data *ocpc,
						 int current_ma,
						 int voltage_mv);

/**
 * Is the input current limit been reached?
 *
 * @param chgnum: Active charge port
 * @param reached: Pointer to reached
 * @return EC_SUCCESS on success, error otherwise.
 */
enum ec_error_list charger_is_icl_reached(int chgnum, bool *reached);

/**
 * Enable/disable linear charging
 *
 * For charger ICs that support it, this allows the charger IC to operate the
 * BFET in the linear region.
 *
 * @param chgnum: Active charge port
 * @param enable: Whether to enable or disable linear charging.
 * @return EC_SUCCESS on success, error otherwise.
 */
enum ec_error_list charger_enable_linear_charge(int chgnum, bool enable);

/**
 * Enable/disable bypass mode
 *
 * Bypass mode allows AC power to be supplied directly to the system rail
 * instead of going through the charger.
 *
 * @param chgnum: Active charge port
 * @param enable: Whether to enable or disable bypass mode.
 * @return EC_SUCCESS on success, error otherwise.
 */
enum ec_error_list charger_enable_bypass_mode(int chgnum, bool enable);

/**
 * Get the charger configuration for the number of battery cells
 *
 * The default charging mode is configured by sensing an external
 * resistor. The number of battery cells can be determined from the
 * charging mode.
 *
 * @param chgnum: Active charge port.
 * @param cells: The number of battery cells.
 *
 * @return EC_SUCCESS on success, an error otherwise.
 */
enum ec_error_list charger_get_battery_cells(int chgnum, int *cells);

/*
 * Print all charger info for debugging purposes
 * @param chgnum: charger IC index.
 */
void print_charger_debug(int chgnum);

/*
 * Print prochot status for debugging purposes
 * @param chgnum: charger IC index.
 */
void print_charger_prochot(int chgnum);

/**
 * Get the value of CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON
 */
int charger_get_min_bat_pct_for_power_on(void);

/* Wake up the task when something important happens */
void charge_wakeup(void);

/*
 * Ask the charger for some voltage and current. If either value is 0,
 * charging is disabled; otherwise it's enabled. Negative values are ignored.
 *
 * @param use_curr Use values from requested voltage and current (otherwise use
 * 0 for both)
 * @param is_full Battery is full
 */
int charge_request(bool use_curr, bool is_full);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CHARGER_H */
