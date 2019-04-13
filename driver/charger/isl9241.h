/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas (Intersil) ISL-9241 battery charger driver header.
 */

#ifndef __CROS_EC_ISL9241_H
#define __CROS_EC_ISL9241_H

#define ISL9241_ADDR	0x12 /* 7bit address 0001001 */
#define I2C_ADDR_CHARGER ISL9241_ADDR

#define CHARGER_NAME	"ISL9241"
#define CHARGE_V_MAX	18304
#define CHARGE_V_MIN	64
#define CHARGE_V_STEP	8
#define CHARGE_I_MAX	6140
#define CHARGE_I_MIN	4
#define CHARGE_I_STEP	4
#define INPUT_I_MAX	6140
#define INPUT_I_MIN	4
#define	INPUT_I_STEP	4

/* Registers */

/*
 * ChargeCurrentLimit [12:2] 11-bit (0x0000h = disables fast charging,
 * trickle charging is allowed)
 */
#define ISL9241_REG_CHG_CURRENT_LIMIT	0x14

/* MaxSystemVoltage [14:3] 12-bit, (0x0000h = disables switching) */
#define ISL9241_REG_MAX_SYSTEM_VOLTAGE	0x15

#define ISL9241_REG_CONTROL7		0x38
#define ISL9241_REG_CONTROL0		0x39
#define ISL9241_REG_INFORMATION1	0x3A
#define ISL9241_REG_ADAPTER_CUR_LIMIT2	0x3B

/* Configures various charger options */
#define ISL9241_REG_CONTROL1		0x3C
#define ISL9241_CONTROL1_LEARN_MODE	BIT(12)

/* Configures various charger options */
#define ISL9241_REG_CONTROL2		0x3D
/*
 * 15:13 - Trickle Charging Current
 *         <000> 32mA (do not use)
 *         <001> 64mA
 *         <010> 96mA
 *         <011> 128mA (default)
 *         <100> 160mA
 *         <101> 192mA
 *         <110> 224mA
 *         <111> 256mA
 */
#define ISL9241_CONTROL2_TRICKLE_CHG_CURR(curr)	((((curr) >> 5) - 1) << 13)
/* 12 - Two-Level Adapter Current Limit */
#define ISL9241_CONTROL2_TWO_LEVEL_ADP_CURR	BIT(12)
/* 10:9 PROCHOT# debounce time in uS */
#define ISL9241_CONTROL2_PROCHOT_DEBOUNCE_1000	(3 << 9)

/* MinSystemVoltage [13:6] 8-bit (0x0000h = disables all battery charging) */
#define ISL9241_REG_MIN_SYSTEM_VOLTAGE	0x3E

#define ISL9241_REG_ADAPTER_CUR_LIMIT1	0x3F
#define ISL9241_REG_ACOK_REFERENCE	0x40
#define ISL9241_REG_CONTROL6		0x43
#define ISL9241_REG_AC_PROCHOT		0x47
#define ISL9241_REG_DC_PROCHOT		0x48
#define ISL9241_REG_OTG_VOLTAGE		0x49
#define ISL9241_REG_OTG_CURRENT		0x4A
#define ISL9241_REG_VIN_VOLTAGE		0x4B

/* Configures various charger options */
#define ISL9241_REG_CONTROL3		0x4C
/* 14: ACLIM Reload (0 - reload, 1 - Do not reload */
#define ISL9241_CONTROL3_ACLIM_RELOAD	BIT(14)
/* 2: Digital Reset (0 - Idle, 1 - Reset */
#define ISL9241_CONTROL3_DIGITAL_RESET	BIT(2)

/* Indicates various charger status */
#define ISL9241_REG_INFORMATION2	0x4D
/* 12: BATGONE pin status (0 = Battery is present, 1 = No battery) */
#define ISL9241_INFORMATION2_BATGONE_PIN	BIT(12)
/* 14: ACOK pin status (0 = No adapter, 1 = Adapter is present) */
#define ISL9241_INFORMATION2_ACOK_PIN		BIT(14)

#define ISL9241_REG_CONTROL4		0x4E
#define ISL9241_REG_CONTROL5		0x4F
#define ISL9241_REG_NTC_ADC_RESULTS	0x80
#define ISL9241_REG_VBAT_ADC_RESULTS	0x81
#define ISL9241_REG_TJ_ADC_RESULTS	0x82
#define ISL9241_REG_IADP_ADC_RESULTS	0x83
#define ISL9241_REG_DC_ADC_RESULTS	0x84
#define ISL9241_REG_CC_ADC_RESULTS	0x85
#define ISL9241_REG_VSYS_ADC_RESULTS	0x86
#define ISL9241_REG_VIN_ADC_RESULTS	0x87
#define ISL9241_REG_INFORMATION3	0x90
#define ISL9241_REG_INFORMATION4	0x91
#define ISL9241_REG_MANUFACTURER_ID	0xFE
#define ISL9241_REG_DEVICE_ID		0xFF

#endif /* __CROS_EC_ISL9241_H */
