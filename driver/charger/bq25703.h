/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25703 battery charger driver.
 */

#ifndef __CROS_EC_BQ25703_H
#define __CROS_EC_BQ25703_H

/* I2C Interface */
#define BQ25703_I2C_ADDR1_FLAGS 0x6B

/*
 * BC1.2 minimum voltage threshold for BQ25703.
 * BC1.2 charging port output voltage range is 4.75V to 5.25V,
 * BQ25703 Input Voltage Accuracy is -2% to +2% (-95mV to +95mV)
 * 4750mV - 95mV => 4655mV - 3200 (offset reg 0x0A) => 1455mv
 * 1455mv & 0x1FC0 = 1408 (data for register 0x0A)
 */
#define BQ25703_BC12_MIN_VOLTAGE_MV	1408

/* Registers */

/* ChargeOption0 Register */
#define BQ25703_REG_CHARGE_OPTION_0		0x00
#define BQ25703_CHARGE_OPTION_0_LOW_POWER_MODE	BIT(15)
#define BQ25703_CHARGE_OPTION_0_EN_LEARN	BIT(5)
#define BQ25703_CHARGE_OPTION_0_CHRG_INHIBIT	BIT(0)

#define BQ25703_REG_CHARGE_CURRENT		0x02
#define BQ25703_REG_MAX_CHARGE_VOLTAGE		0x04
#define BQ25703_REG_CHARGE_OPTION_1		0x30

/* ChargeOption2 Register */
#define BQ25703_REG_CHARGE_OPTION_2		0x32
#define BQ25703_CHARGE_OPTION_2_EN_EXTILIM	BIT(7)

/* ChargeOption3 Register */
#define BQ25703_REG_CHARGE_OPTION_3		0x34
#define BQ25703_CHARGE_OPTION_3_EN_ICO_MODE	BIT(11)

#define BQ25703_REG_PROCHOT_OPTION_0		0x36
#define BQ25703_REG_PROCHOT_OPTION_1		0x38

/* ADCOption Register */
#define BQ25703_REG_ADC_OPTION			0x3A
#define BQ25703_ADC_OPTION_ADC_START		BIT(14)
#define BQ25703_ADC_OPTION_EN_ADC_IIN		BIT(4)

/* ChargeStatus Register */
#define BQ25703_REG_CHARGER_STATUS		0x20
#define BQ25703_CHARGE_STATUS_ICO_DONE		BIT(14)

#define BQ25703_REG_PROCHOT_STATUS		0x22
#define BQ25703_REG_IIN_DPM			0x25
#define BQ25703_REG_ADC_PSYS			0x26
#define BQ25703_REG_ADC_VBUS			0x27
#define BQ25703_REG_ADC_IBAT			0x28
#define BQ25703_REG_ADC_CMPIN			0x2A

/* ADCIIN Register */
#define BQ25703_REG_ADC_IIN			0x2B
#define BQ25703_ADC_IIN_STEP_MA			50

#define BQ25703_REG_ADC_VSYS_VBAT		0x2C
#define BQ25703_REG_OTG_VOLTAGE			0x06
#define BQ25703_REG_OTG_CURRENT			0x08
#define BQ25703_REG_INPUT_VOLTAGE		0x0A
#define BQ25703_REG_MIN_SYSTEM_VOLTAGE		0x0C
#define BQ25703_REG_IIN_HOST			0x0F
#define BQ25703_REG_MANUFACTURER_ID		0x2E
#define BQ25703_REG_DEVICE_ADDRESS		0x2F

#endif /* __CROS_EC_BQ25703_H */
