/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TPSChrome PMU APIs.
 */

#ifndef __CROS_EC_TPSCHROME_H
#define __CROS_EC_TPSCHROME_H

#define FET_BACKLIGHT 1
#define FET_LCD_PANEL 6

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
 *  * Initialize pmu
 *   */
void pmu_init(void);

#endif /* __CROS_EC_TPSCHROME_H */

