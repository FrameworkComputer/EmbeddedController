/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25710 battery charger driver.
 */

#ifndef __CROS_EC_BQ25710_H
#define __CROS_EC_BQ25710_H

/* SMBUS Interface */
#define BQ25710_SMBUS_ADDR1_FLAGS 0x09

#define BQ25710_BC12_MIN_VOLTAGE_MV 1408

/* Registers */
#define BQ25710_REG_CHARGE_OPTION_0 0x12
#define BQ25710_REG_CHARGE_CURRENT 0x14
#define BQ25710_REG_MAX_CHARGE_VOLTAGE 0x15
#define BQ25770_REG_CHARGE_PROFILE 0x17
#define BQ25770_REG_GATEDRIVE 0x18
#define BQ25770_REG_CHARGE_OPTION_5 0x19
#define BQ25770_REG_AUTO_CHARGE 0x1A
#define BQ25770_REG_CHARGER_STATUS_0 0x1B
#define BQ25710_REG_CHARGER_STATUS 0x20
#define BQ25710_REG_PROCHOT_STATUS 0x21
#define BQ25710_REG_IIN_DPM 0x22
#define BQ25710_REG_ADC_VBUS_PSYS 0x23
#define BQ25710_REG_ADC_IBAT 0x24
#define BQ25710_REG_ADC_CMPIN_IIN 0x25
#define BQ25710_REG_ADC_VSYS_VBAT 0x26
#define BQ25770_REG_ADC_VBAT 0x27
#define BQ25770_REG_ADC_PSYS 0x28
#define BQ25770_REG_ADC_CMPIN_TR 0x29
#define BQ25710_REG_CHARGE_OPTION_1 0x30
#define BQ25710_REG_CHARGE_OPTION_2 0x31
#define BQ25710_REG_CHARGE_OPTION_3 0x32
#define BQ25710_REG_PROCHOT_OPTION_0 0x33
#define BQ25710_REG_PROCHOT_OPTION_1 0x34
#define BQ25710_REG_ADC_OPTION 0x35
#define BQ25720_REG_CHARGE_OPTION_4 0x36
#define BQ25720_REG_VMIN_ACTIVE_PROTECTION 0x37
#define BQ25710_REG_OTG_VOLTAGE 0x3B
#define BQ25710_REG_OTG_CURRENT 0x3C
#define BQ25710_REG_INPUT_VOLTAGE 0x3D
#define BQ25710_REG_MIN_SYSTEM_VOLTAGE 0x3E
#define BQ25710_REG_IIN_HOST 0x3F
#define BQ25770_REG_AUTOTUNE_FORCE 0x61
#define BQ25770_REG_GM_ADJUST_FORCE 0x62
#define BQ25710_REG_MANUFACTURER_ID 0xFE
#define BQ25710_REG_DEVICE_ADDRESS 0xFF

/* ADC conversion time ins ms */
#if defined(CONFIG_CHARGER_BQ25720)
#define BQ25710_ADC_OPTION_ADC_CONV_MS 25
#elif defined(CONFIG_CHARGER_BQ25710)
#define BQ25710_ADC_OPTION_ADC_CONV_MS 10
#elif defined(CONFIG_CHARGER_BQ25770)
#define BQ25710_ADC_OPTION_ADC_CONV_MS 12
#else
#error Only the BQ25720 and BQ25710 are supported by bq25710 driver.
#endif

/* ADCVBUS/PSYS Register */
#if defined(CONFIG_CHARGER_BQ25720)
#define BQ25720_ADC_VBUS_STEP_MV 96
#elif defined(CONFIG_CHARGER_BQ25710)
#define BQ25710_ADC_VBUS_STEP_MV 64
#define BQ25710_ADC_VBUS_BASE_MV 3200
#elif defined(CONFIG_CHARGER_BQ25770)
#define BQ25770_ADC_VBUS_STEP_MV 2
#else
#error Only the BQ25720 and BQ25710 are supported by bq25710 driver.
#endif

/* Min System Voltage Register */
#define BQ25710_MIN_SYSTEM_VOLTAGE_STEP_MV 256
#define BQ25720_VSYS_MIN_VOLTAGE_STEP_MV 100
#define BQ25770_VSYS_MIN_VOLTAGE_STEP_MV 5

extern const struct charger_drv bq25710_drv;

/**
 * Set VSYS_MIN
 *
 * @param chgnum: Index into charger chips
 * @param mv: min system voltage in mV
 * @return EC_SUCCESS or error
 */
int bq25710_set_min_system_voltage(int chgnum, int mv);

/**
 * Enable or disable Out-of-Audio.
 *
 * @param chgnum: Index into charger chips
 * @param enable true to enable OOA, false to disable.
 * @return EC_SUCCESS or error
 */
int bq25710_set_ooa(int chgnum, bool enable);

#endif /* __CROS_EC_BQ25710_H */
