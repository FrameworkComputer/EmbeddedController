/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(AC_PRESENT,		PIN(C, 1), GPIO_INT_BOTH,			extpower_interrupt)
GPIO_INT(POWER_BUTTON_L,	PIN(0, 4), GPIO_INT_BOTH | GPIO_PULL_UP,	power_button_interrupt)	/* MECH_PWR_BTN_ODL */

GPIO_INT(PCH_SLP_S0_L,		PIN(7, 5), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(PCH_SLP_S3_L,		PIN(7, 3), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(PCH_SLP_S4_L,		PIN(8, 6), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(PCH_SLP_SUS_L,		PIN(6, 2), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(RSMRST_L_PGOOD,	PIN(B, 0), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(PMIC_DPWROK,		PIN(C, 7), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(WP_L,			PIN(9, 3), GPIO_INT_BOTH,			switch_interrupt)
GPIO_INT(RECOVERY_L,		PIN(8, 2), GPIO_INT_BOTH, button_interrupt)	/* Recovery button */

/* TODO(jnchase): configure as interrupt when code is ready / if needed*/
GPIO(POE_LTC_PGOOD,		PIN(C, 5), GPIO_INPUT)				/* PoE power good */
GPIO(PSE_PWM_INT,		PIN(3, 7), GPIO_INPUT)				/* PoE LTC interrupt */
GPIO(V3P3A_I350_PG,		PIN(4, 4), GPIO_INPUT)				/* Disconnected */
GPIO(USB_C0_VBUS_DET_L,		PIN(9, 7), GPIO_INPUT)				/* USB-C VBUS */
GPIO(USB_C0_POL_L,		PIN(3, 3), GPIO_INPUT | GPIO_SEL_1P8V)		/* USB-C Polarity */

GPIO(PCH_RTCRST,		PIN(E, 7), GPIO_OUT_LOW)			/* RTCRST# to SOC */
GPIO(WLAN_OFF_L,		PIN(7, 2), GPIO_OUT_LOW)			/* Disable WLAN */
GPIO(PP3300_DX_WLAN,		PIN(A, 7), GPIO_OUT_LOW)			/* Enable WLAN 3.3V Power */
GPIO(CPU_PROCHOT,		PIN(8, 1), GPIO_OUT_HIGH)			/* PROCHOT# to SOC */
GPIO(PCH_ACPRESENT,		PIN(5, 0), GPIO_ODR_LOW)			/* ACOK to SOC */
GPIO(PCH_WAKE_L,		PIN(A, 3), GPIO_ODR_HIGH)			/* Wake SOC */
GPIO(PCH_RSMRST_L,		PIN(7, 0), GPIO_OUT_LOW)			/* RSMRST# to SOC */
GPIO(PCH_PWRBTN_L,		PIN(7, 4), GPIO_ODR_HIGH)			/* Power Button to SOC */
GPIO(EC_PLATFORM_RST,		PIN(4, 5), GPIO_OUT_LOW)			/* EC Reset to LDO_EN */
GPIO(SYS_RESET_L,		PIN(6, 1), GPIO_ODR_HIGH)			/* Cold Reset to SOC */
GPIO(PMIC_SLP_SUS_L,		PIN(8, 5), GPIO_OUT_LOW)			/* SLP_SUS# to PMIC */
GPIO(CCD_MODE_ODL,		PIN(6, 3), GPIO_INPUT)				/* Case Closed Debug Mode */
GPIO(EC_HAVEN_RESET_ODL,	PIN(0, 2), GPIO_ODR_HIGH)			/* H1 Reset */
GPIO(ENTERING_RW,		PIN(7, 6), GPIO_OUTPUT)				/* EC Entering RW */
GPIO(PMIC_INT_L,		PIN(6, 0), GPIO_INPUT)				/* PMIC interrupt */
GPIO(EC_RST_LTC4291_L,		PIN(9, 4), GPIO_OUT_HIGH)			/* PSE controller reset */

GPIO(POWER_RATE,		PIN(7, 1), GPIO_INPUT)				/* High: i3/5/7. Low: Celeron */
GPIO(PP3300_USB_PD_EN,		PIN(6, 7), GPIO_OUT_HIGH)                       /* Initialize PP3300_USB_PD_EN as output high */

GPIO(LAN_PWR_EN,		PIN(8, 3), GPIO_OUT_HIGH)			/* Ethernet power enabled */

/* I2C pins - these will be reconfigured for alternate function below */
GPIO(I2C0_0_SCL,		PIN(B, 5), GPIO_INPUT)				/* EC_I2C_LTC_SCL */
GPIO(I2C0_0_SDA,		PIN(B, 4), GPIO_INPUT)				/* EC_I2C_LTC_SDA */
GPIO(I2C0_1_SCL,		PIN(B, 3), GPIO_INPUT)				/* EC_I2C_EEPROM_SCL */
GPIO(I2C0_1_SDA,		PIN(B, 2), GPIO_INPUT)				/* EC_I2C_EEPROM_SDA */
GPIO(I2C1_SCL,			PIN(9, 0), GPIO_INPUT)				/* TP184 */
GPIO(I2C1_SDA,			PIN(8, 7), GPIO_INPUT)				/* TP185 */
GPIO(I2C2_SCL,			PIN(9, 2), GPIO_INPUT)				/* EC_ROP_I2C_CLK */
GPIO(I2C2_SDA,			PIN(9, 1), GPIO_INPUT)				/* EC_ROP_I2C_SDA */
GPIO(I2C3_SCL,			PIN(D, 1), GPIO_INPUT)				/* EC_THEM_CLK */
GPIO(I2C3_SDA,			PIN(D, 0), GPIO_INPUT)				/* EC_THEM_SDA */

/* 5V enables: INPUT=1.5A, OUT_LOW=OFF, OUT_HIGH=3A */
GPIO(USB_C0_5V_EN,		PIN(4, 2), GPIO_OUT_LOW | GPIO_PULL_UP)		/* C0 5V Enable */
GPIO(USB_C0_VBUS_ILIM,		PIN(3, 5), GPIO_OUT_HIGH)
GPIO(FAN_PWR_EN,		PIN(9, 5), GPIO_OUT_HIGH)			/* Fan power */
GPIO(PI3_BC12_DET_L,		PIN(D, 3), GPIO_INPUT)				/* USB-C */
GPIO(USB_FP3_CHARGE_EN_L,	PIN(C, 6), GPIO_OUT_LOW)			/* USB-C */
GPIO(USB_FP0_5V_EN,		PIN(0, 0), GPIO_OUT_LOW)			/* Front port 1 */
GPIO(USB_FP1_5V_EN,		PIN(B, 1), GPIO_OUT_LOW)			/* Front port 2 */
GPIO(USB_FP3_5V_EN,		PIN(A, 1), GPIO_OUT_LOW)			/* Front port 3 */
GPIO(USB_FP_CHARGE_EN_L,	PIN(A, 5), GPIO_OUT_LOW)			/* USB-A */
GPIO(PP3300_TPU_EN,		PIN(0, 1), GPIO_OUT_HIGH)			/* TPU 3.3V enable */

/* Not connected */
GPIO(USB_C0_CHARGE_L,		PIN(C, 0), GPIO_OUT_LOW)			/* C0 Charge enable */
GPIO(AC_JACK_CHARGE_L,		PIN(C, 3), GPIO_OUT_LOW)			/* AC jack charge enable */
GPIO(USB_C0_PD_RST_ODL,		PIN(0, 3), GPIO_OUT_LOW)			/* C0 PD Reset */
GPIO(USB_C0_TCPC_PWR,		PIN(8, 4), GPIO_OUT_LOW)			/* Enable C0 TCPC Power */
GPIO(TYPE_C_65W,		PIN(3, 4), GPIO_OUTPUT | GPIO_PULL_DOWN)

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(6, 0x30), 1, MODULE_UART, 0)		/* GPIO64-65 */		/* UART from EC to Servo */
ALTERNATE(PIN_MASK(9, 0x06), 1, MODULE_I2C, 0)		/* GPIO91-92 */		/* EC_I2C2_PMIC_3V3_SDA/SCL */
ALTERNATE(PIN_MASK(A, 0x40), 1, MODULE_PWM, 0)		/* GPIOA6 */		/* TACH2 */
ALTERNATE(PIN_MASK(B, 0x30), 1, MODULE_I2C, 0)		/* GPIOB4-B5 */		/* EC_I2C0_0_LTC_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x40), 1, MODULE_PWM, 0)		/* GPIOB6 */		/* EC_FAN_PWM */
ALTERNATE(PIN_MASK(B, 0x0C), 1, MODULE_I2C, 0)		/* GPOPB2-B3 */		/* EC_I2C0_1_EEPROM_SDA/SCL */
ALTERNATE(PIN_MASK(D, 0x03), 1, MODULE_I2C, 0)		/* GPIOD0-D1 */		/* EC_I2C3_SENSOR_1V8_SDA/SCL */
/* Alternate functions for LED PWM */
ALTERNATE(PIN_MASK(8, 0x01), 1, MODULE_PWM, 0)		/* GPIO80 PWM3 Red*/
ALTERNATE(PIN_MASK(B, 0x80), 1, MODULE_PWM, 0)		/* GPOB7 PWM5 Green*/
