/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24192 battery charger driver.
 */

#ifndef __CROS_EC_BQ24192_H
#define __CROS_EC_BQ24192_H

#define BQ24192_ADDR_FLAGS 0x6b

/* Registers */
#define BQ24192_REG_INPUT_CTRL      0x0
#define BQ24192_REG_POWER_ON_CFG    0x1
#define BQ24192_REG_CHG_CURRENT     0x2
#define BQ24192_REG_PRE_CHG_CURRENT 0x3
#define BQ24192_REG_CHG_VOLTAGE     0x4
#define BQ24192_REG_CHG_TERM_TMR    0x5
#define BQ24192_REG_IR_COMP         0x6
#define BQ24192_REG_MISC_OP         0x7
#define BQ24192_REG_STATUS          0x8
#define BQ24192_REG_FAULT           0x9
#define BQ24192_REG_ID              0xa

#define BQ24192_DEVICE_ID           0x2b

#endif /* __CROS_EC_BQ24192_H */
