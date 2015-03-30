/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25890/bq25892/bq25895 battery charger driver.
 */

#ifndef __CROS_EC_CHARGER_BQ2589X_H
#define __CROS_EC_CHARGER_BQ2589X_H

/* Registers */
#define BQ2589X_REG_INPUT_CURR      0x00
#define BQ2589X_REG_VINDPM          0x01
#define BQ2589X_REG_CFG1            0x02
#define BQ2589X_REG_CFG2            0x03
#define BQ2589X_REG_CHG_CURR        0x04
#define BQ2589X_REG_PRE_CHG_CURR    0x05
#define BQ2589X_REG_CHG_VOLT        0x06
#define BQ2589X_REG_TIMER           0x07
#define BQ2589X_REG_IR_COMP         0x08
#define BQ2589X_REG_FORCE           0x09
#define BQ2589X_REG_BOOST_MODE      0x0A
#define BQ2589X_REG_STATUS          0x0B /* Read-only */
#define BQ2589X_REG_FAULT           0x0C /* Read-only */
#define BQ2589X_REG_VINDPM_THRESH   0x0D
#define BQ2589X_REG_ADC_BATT_VOLT   0x0E /* Read-only */
#define BQ2589X_REG_ADC_SYS_VOLT    0x0F /* Read-only */
#define BQ2589X_REG_ADC_TS          0x10 /* Read-only */
#define BQ2589X_REG_ADC_VBUS_VOLT   0x11 /* Read-only */
#define BQ2589X_REG_ADC_CHG_CURR    0x12 /* Read-only */
#define BQ2589X_REG_ADC_INPUT_CURR  0x13 /* Read-only */
#define BQ2589X_REG_ID              0x14

#define BQ2589X_DEVICE_ID_MASK      0x38
#define BQ25890_DEVICE_ID           0x18
#define BQ25892_DEVICE_ID           0x00
#define BQ25895_DEVICE_ID           0x38

/* Variant-specific configuration */
#if   defined(CONFIG_CHARGER_BQ25890)
#define BQ2589X_DEVICE_ID    BQ25890_DEVICE_ID
#define BQ2589X_ADDR         (0x6A << 1)
#elif defined(CONFIG_CHARGER_BQ25895)
#define BQ2589X_DEVICE_ID    BQ25895_DEVICE_ID
#define BQ2589X_ADDR         (0x6A << 1)
#elif defined(CONFIG_CHARGER_BQ25892)
#define BQ2589X_DEVICE_ID    BQ25892_DEVICE_ID
#define BQ2589X_ADDR         (0x6B << 1)
#else
#error BQ2589X unknown variant
#endif

#endif /* __CROS_EC_CHARGER_BQ2589X_H */
