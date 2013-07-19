/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TPSChrome PMU APIs.
 */

#ifndef __CROS_EC_TPSCHROME_H
#define __CROS_EC_TPSCHROME_H

#include "gpio.h"

/* Non-SBS charging states */
enum charging_state {
	ST_IDLE0,
	ST_IDLE,
	ST_BAD_COND,
	ST_PRE_CHARGING,
	ST_CHARGING,
	ST_CHARGING_ERROR,
	ST_DISCHARGING,
};

/* Debugging constants, in the same order as enum power_state. This string
 * table was moved here to sync with enum above.
 */
#define POWER_STATE_NAME_TABLE  \
	{			\
		"idle0",	\
		"idle",		\
		"bad cond",	\
		"pre-charging",	\
		"charging",	\
		"charging error", \
		"discharging"	\
	}
	/* End of POWER_STATE_NAME_TABLE macro */

/* JEITA temperature threshold */
enum TPS_TEMPERATURE {
	TSET_T1,
	TSET_T2,
	TSET_T3,
	TSET_T4,
};

/* JEITA temperature range */
enum TPS_TEMPERATURE_RANGE {
	RANGE_T01,
	RANGE_T12,		/* low charging temperature range */
	RANGE_T23,		/* standard charging temperature range */
	RANGE_T34,		/* high charging temperature range */
	RANGE_T40,
};

/* Termination voltage */
enum TPS_TERMINATION_VOLTAGE {
	TERM_V2000,		/* 2.000 V */
	TERM_V2050,		/* 2.050 V */
	TERM_V2075,		/* 2.075 V */
	TERM_V2100,		/* 2.100 V */
};

/* Termination current */
enum TPS_TERMINATION_CURRENT {
	TERM_I0000,		/* 0    % */
	TERM_I0250,		/* 25   % */
	TERM_I0375,		/* 37.5 % */
	TERM_I0500,		/* 50   % */
	TERM_I0625,		/* 62.5 % */
	TERM_I0750,		/* 75   % */
	TERM_I0875,		/* 87.5 % */
	TERM_I1000,		/* 100  % */
};

/* Fast charge timeout */
enum FASTCHARGE_TIMEOUT {
	TIMEOUT_2HRS,
	TIMEOUT_3HRS,
	TIMEOUT_4HRS,
	TIMEOUT_5HRS,
	TIMEOUT_6HRS,
	TIMEOUT_7HRS,
	TIMEOUT_8HRS,
	TIMEOUT_10HRS,          /* No 9 hours option */
};

#define FET_BACKLIGHT 1
#define FET_WWAN      3
#define FET_VIDEO     4
#define FET_CAMERA    5
#define FET_LCD_PANEL 6
#define FET_TS        7

#define ADC_VAC		0
#define ADC_VBAT	1
#define ADC_IAC		2
#define ADC_IBAT	3
#define ADC_IDCDC1	4
#define ADC_IDCDC2	5
#define ADC_IDCDC3	6
#define ADC_IFET1	7
#define ADC_IFET2	8
#define ADC_IFET3	9
#define ADC_IFET4	10
#define ADC_IFET5	11
#define ADC_IFET6	12
#define ADC_IFET7	13

/* do not turn off voltage reference */
#define ADC_FLAG_KEEP_ON	0x1

/**
 * Clear tps65090 IRQ register
 *
 * @return              return EC_SUCCESS on success, err code otherwise
 */
int pmu_clear_irq(void);

/**
 * Read pmu register
 *
 * @param reg           register offset
 * @param value         pointer to output value
 * @return              return EC_SUCCESS on success, err code otherwise
 */
int pmu_read(int reg, int *value);

/**
 * Write pmu register
 *
 * @param reg           register offset
 * @param value         new register value
 * @return              return EC_SUCCESS on success, err code otherwise
 */
int pmu_write(int reg, int value);

/**
 * Read tpschrome version
 *
 * @param version       output tpschrome version info
 */
int pmu_version(int *version);

/**
 * Check pmu charger alarm
 *
 * @return 0 if there's no charging alarm or pmu access failed
 * @return 1 if charger over current or over heat
 */
int pmu_is_charger_alarm(void);

/**
 * Check pmu charge timeout
 *
 * @return 1 if charge timed out
 */
int pmu_is_charge_timeout(void);

/**
 * Get pmu power source
 *
 * @param ac_good	pointer to output value of ac voltage good
 * @param battery_good	pointer to output value of battery voltage good
 * @return EC_SUCCESS if ac_good and battery_good are set
 */
int pmu_get_power_source(int *ac_good, int *battery_good);

/**
 * Enable/disable pmu fet
 *
 * @param fet_id	the fet to control
 * @param enable	0 to disable the fet, 1 to enable
 * @param power_good	pointer to value of fet power good
 * @return		EC_SUCCESS if the communication to pmu succeeded
 */
int pmu_enable_fet(int fet_id, int enable, int *power_good);

/**
 * Enable/disable pmu internal charger force charging mode
 *
 * @param enable        0 to disable the charger, 1 to enable
 * @return              EC_SUCCESS if no I2C communication error
 */
int pmu_enable_charger(int enable);

/**
 * Set termination current for temperature ranges
 *
 * @param range           T01 T12 T23 T34 T40
 * @param current         enum termination current, I0250 == 25.0%:
 *                        I0000 I0250 I0375 I0500 I0625 I0750 I0875 I1000
 */
int pmu_set_term_current(enum TPS_TEMPERATURE_RANGE range,
		enum TPS_TERMINATION_CURRENT current);

/**
 * Set termination voltage for temperature ranges
 *
 * @param range           T01 T12 T23 T34 T40
 * @param voltage         enum termination voltage, V2050 == 2.05V:
 *                        V2000 V2050 V2075 V2100
 */
int pmu_set_term_voltage(enum TPS_TEMPERATURE_RANGE range,
		enum TPS_TERMINATION_VOLTAGE voltage);

/**
 * Enable low current charging
 *
 * @param enable         enable/disable low current charging
 */
int pmu_low_current_charging(int enable);

/**
 * Read ADC channel
 *
 * @param adc_idx        Index of ADC channel
 * @param flags          combination of ADC_FLAG_* constants
 */
int pmu_adc_read(int adc_idx, int flags);

#ifdef HAS_TASK_CHARGER
/**
 * Handles charger interrupts from tpschrome
 *
 * @param signal         Indicates signal type.
 */
void pmu_irq_handler(enum gpio_signal signal);
#else
#define pmu_irq_handler NULL
#endif

/**
 * Set temperature threshold
 *
 * @param temp_n          TSET_T1 to TSET_T4
 * @param value           0b000 ~ 0b111, temperature threshold
 */
int pmu_set_temp_threshold(enum TPS_TEMPERATURE temp_n, uint8_t value);

/**
 * Force charger into error state, turn off charging and blinks charging LED
 *
 * @param enable          true to turn off charging and blink LED
 * @return                EC_SUCCESS if ok
 */
int pmu_blink_led(int enable);

/**
 *  * Initialize pmu
 *   */
void pmu_init(void);

/**
 * Shut down the pmu, by resetting it's registers to disable it's FETs,
 * DCDCs and ADC.
 */
int pmu_shutdown(void);

/**
 * Set external charge enable pin
 *
 * @param enable        boolean, set 1 to eanble external control
 */
int pmu_enable_ext_control(int enable);

/**
 * Set fast charge timeout
 *
 * @param timeout         enum FASTCHARGE_TIMEOUT
 */
int pmu_set_fastcharge(enum FASTCHARGE_TIMEOUT timeout);

/**
 * Wake TPS65090 charger task, but throttled to at most one call per tick
 */
void pmu_task_throttled_wake(void);

/**
 * Get current charge state
 */
enum charging_state charge_get_state(void);

/**
 * Return non-zero if battery is so low we want to keep AP off.
 */
int charge_keep_power_off(void);

/**
 * Initialize PMU registers using board settings.
 *
 * Boards must supply this function.  This will be called from pmu_init().
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int pmu_board_init(void);

#endif /* __CROS_EC_TPSCHROME_H */

