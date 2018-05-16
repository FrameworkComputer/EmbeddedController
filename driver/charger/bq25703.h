/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25703 battery charger driver.
 */

#ifndef __CROS_EC_BQ25703_H
#define __CROS_EC_BQ25703_H

/* I2C Interface */
#define BQ25703_I2C_ADDR1 0xD6

/* Registers */
#define BQ25703_REG_CHARGE_OPTION_0		0x00
#define BQ25703_REG_CHARGE_CURRENT		0x02
#define BQ25703_REG_MAX_CHARGE_VOLTAGE		0x04
#define BQ25703_REG_CHARGE_OPTION_1		0x30
#define BQ25703_REG_CHARGE_OPTION_2		0x32
#define BQ25703_REG_CHARGE_OPTION_3		0x34
#define BQ25703_REG_PROCHOT_OPTION_0		0x36
#define BQ25703_REG_PROCHOT_OPTION_1		0x38
#define BQ25703_REG_ADC_OPTION			0x3A
#define BQ25703_REG_CHARGER_STATUS		0x20
#define BQ25703_REG_PROCHOT_STATUS		0x22
#define BQ25703_REG_IIN_DPM			0x25
#define BQ25703_REG_ADC_PSYS			0x26
#define BQ25703_REG_ADC_VBUS			0x27
#define BQ25703_REG_ADC_IBAT			0x28
#define BQ25703_REG_ADC_CMPIN			0x2A
#define BQ25703_REG_ADC_IIN			0x2B
#define BQ25703_REG_ADC_VSYS_VBAT		0x2C
#define BQ25703_REG_OTG_VOLTAGE			0x06
#define BQ25703_REG_OTG_CURRENT			0x08
#define BQ25703_REG_INPUT_VOLTAGE		0x0A
#define BQ25703_REG_MIN_SYSTEM_VOLTAGE		0x0C
#define BQ25703_REG_IIN_HOST			0x0F
#define BQ25703_REG_MANUFACTURER_ID		0x2E
#define BQ25703_REG_DEVICE_ADDRESS		0x2F

/* ChargeOption0 Register */
#define BQ25703_CHARGE_OPTION_0_EN_LEARN	(1 << 5)
#define BQ25703_CHARGE_OPTION_0_CHRG_INHIBIT	(1 << 0)

/* ChargeOption3 Register */
#define BQ25703_CHARGE_OPTION_3_EN_ICO_MODE	(1 << 11)

/* ChargeStatus Register */
#define BQ25703_CHARGE_STATUS_ICO_DONE		(1 << 14)

/* ADCOption Register */
#define BQ25703_ADC_OPTION_ADC_START		(1 << 14)
#define BQ25703_ADC_OPTION_EN_ADC_IIN		(1 << 4)

/* ADCIIN Register */
#define BQ25703_ADC_IIN_STEP_MA			50

#endif /* __CROS_EC_BQ25703_H */
