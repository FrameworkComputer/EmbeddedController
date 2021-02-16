/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INA231S_H
#define __CROS_EC_INA231S_H

/*
 * Initialize the INA231s
 */
void init_ina231s(void);

/*
 * Return dut vbus voltage in milliVolts
 */
int pp_dut_voltage(void);

/*
 * Return current in milliAmps
 */
int pp_dut_current(void);

/*
 * Return power in milliWatts
 */
int pp_dut_power(void);

/*
 * Return bus voltage in milliVolts
 */
int pp_chg_voltage(void);

/*
 * Return current in milliAmps
 */
int pp_chg_current(void);

/*
 * Return power in milliWatts
 */
int pp_chg_power(void);

/*
 * Return bus voltage in milliVolts
 */
int sr_chg_voltage(void);

/*
 * Return current in milliAmps
 */
int sr_chg_current(void);

/*
 * Return power in milliWatts
 */
int sr_chg_power(void);

#endif /* __CROS_EC_INA231S_H */
