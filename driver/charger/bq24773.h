/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24773 battery charger driver.
 */

#ifndef __CROS_EC_CHARGER_BQ24773_H
#define __CROS_EC_CHARGER_BQ24773_H

/* I2C address */
#define BQ24773_ADDR (0x6a << 1)

/* Chip specific commands */
#define BQ24773_CHARGE_OPTION0          0x00
#define BQ24773_CHARGE_OPTION1          0x02
#define BQ24773_PROCHOT_OPTION0         0x04
#define BQ24773_PROCHOT_OPTION1         0x06
#define BQ24773_PROCHOT_STATUS          0x08
#define BQ24773_DEVICE_ADDRESS          0x09
#define BQ24773_CHARGE_CURRENT          0x0A
#define BQ24773_MAX_CHARGE_VOLTAGE      0x0C
#define BQ24773_MIN_SYSTEM_VOLTAGE      0x0E
#define BQ24773_INPUT_CURRENT           0x0F
#define BQ24773_CHARGE_OPTION2          0x10

/* Option bits */
#define OPTION0_CHARGE_INHIBIT          (1 << 0)
#define OPTION0_LEARN_ENABLE            (1 << 5)

#define OPTION2_EN_EXTILIM              (1 << 7)

/* ChargeCurrent Register - 0x14 (mA) */
#define CHARGE_I_OFF                    0
#define CHARGE_I_MIN                    128
#define CHARGE_I_MAX                    8128
#define CHARGE_I_STEP                   64

/* MaxChargeVoltage Register - 0x15 (mV) */
#define CHARGE_V_MIN                    1024
#define CHARGE_V_MAX                    19200
#define CHARGE_V_STEP                   16

/* InputCurrent Register - 0x3f (mA) */
#define INPUT_I_MIN                    128
#define INPUT_I_MAX                    8128
#define INPUT_I_STEP                   64

#endif /* __CROS_EC_CHARGER_BQ24773_H */
