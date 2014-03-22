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
#define BQ24773_PROTECT_OPTION0         0x04
#define BQ24773_PROTECT_OPTION1         0x06
#define BQ24773_PROTECT_STATUS          0x08
#define BQ24773_DEVICE_ADDRESS          0x09
#define BQ24773_CHARGE_CURRENT          0x0A
#define BQ24773_MAX_CHARGE_VOLTAGE      0x0C
#define BQ24773_MIN_SYSTEM_VOLTAGE      0x0E
#define BQ24773_INPUT_CURRENT           0x0F
#define BQ24773_CHARGE_OPTION2          0x10

/* Option bits */
#define OPTION0_CHARGE_INHIBIT          (1 << 0)
#define OPTION0_LEARN_ENABLE            (1 << 5)

#endif /* __CROS_EC_CHARGER_BQ24773_H */
