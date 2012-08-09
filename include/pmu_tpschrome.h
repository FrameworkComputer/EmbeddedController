/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TPSChrome PMU APIs.
 */

#ifndef __CROS_EC_TPSCHROME_H
#define __CROS_EC_TPSCHROME_H

#include "gpio.h"

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

#define FET_BACKLIGHT 1
#define FET_LCD_PANEL 6


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
 * Enable/disable pmu internal charger
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
 * Handles interrupts from tpschrome
 *
 * @param signal         Indicates signal type.
 */
void pmu_irq_handler(enum gpio_signal signal);

/**
 * Get AC state through GPIO
 *
 * @return 0        AC off
 * @return 1        AC on
 *
 * TODO: This is a board specific function, should be moved to
 * system_common.c or board.c
 */
int pmu_get_ac(void);

/**
 *  * Initialize pmu
 *   */
void pmu_init(void);

#endif /* __CROS_EC_TPSCHROME_H */

