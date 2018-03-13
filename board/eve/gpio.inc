/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(CHARGER_INT_L,		PIN(3, 3), GPIO_INT_FALLING,			bd9995x_vbus_interrupt)
GPIO_INT(USB_C0_PD_INT_ODL,	PIN(3, 7), GPIO_INT_FALLING,			tcpc_alert_event)
GPIO_INT(USB_C1_PD_INT_ODL,	PIN(C, 5), GPIO_INT_FALLING,			tcpc_alert_event)
GPIO_INT(PCH_SLP_S0_L,		PIN(7, 5), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(PCH_SLP_SUS_L,		PIN(6, 2), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(RSMRST_L_PGOOD,	PIN(B, 0), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(PMIC_DPWROK,		PIN(9, 7), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(POWER_BUTTON_L,	PIN(0, 4), GPIO_INT_BOTH | GPIO_PULL_UP,	power_button_interrupt)
GPIO_INT(LID_OPEN,		PIN(6, 7), GPIO_INT_BOTH,			lid_interrupt)
GPIO_INT(TABLET_MODE_L,		PIN(3, 6), GPIO_INT_BOTH,			tablet_mode_interrupt)
/* Volume buttons are swapped in the schematic */
GPIO_INT(VOLUME_DOWN_L,		PIN(8, 2), GPIO_INT_BOTH | GPIO_PULL_UP,	button_interrupt)
GPIO_INT(VOLUME_UP_L,		PIN(8, 3), GPIO_INT_BOTH | GPIO_PULL_UP,	button_interrupt)
GPIO_INT(WP_L,			PIN(4, 0), GPIO_INT_BOTH,			switch_interrupt)
GPIO_INT(AC_PRESENT,		PIN(C, 1), GPIO_INT_BOTH,			extpower_interrupt)
GPIO_INT(ACCELGYRO3_INT_L,	PIN(9, 3), GPIO_INT_FALLING,			bmi160_interrupt)
GPIO_INT(TRACKPAD_INT_L,	PIN(7, 1), GPIO_INT_FALLING,			trackpad_interrupt)
/* DSP IRQ is active low in schematic but DSP treats as active high */
GPIO_INT(MIC_DSP_IRQ_1V8_L,	PIN(C, 6), GPIO_INT_RISING | GPIO_SEL_1P8V,	dsp_interrupt)
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
GPIO_INT(USB_C0_CABLE_DET,      PIN(D, 2), GPIO_INT_RISING,			anx74xx_cable_det_interrupt)
GPIO_INT(USB_C1_CABLE_DET,      PIN(D, 3), GPIO_INT_RISING,			anx74xx_cable_det_interrupt)
#else
GPIO(USB_C0_CABLE_DET,          PIN(D, 2), GPIO_INPUT)
GPIO(USB_C1_CABLE_DET,          PIN(D, 3), GPIO_INPUT)
#endif

/* Lid KCJX9 accelerometer sensor interrupt */
GPIO(ACCEL1_INT_L,		PIN(C, 7), GPIO_INPUT | GPIO_PULL_UP)

GPIO(PCH_RTCRST,		PIN(E, 7), GPIO_OUT_LOW)			/* RTCRST# to SOC */
GPIO(ENABLE_BACKLIGHT,		PIN(5, 6), GPIO_OUT_LOW)			/* Enable Backlight */
GPIO(TRACKPAD_SHDN_L,		PIN(3, 2), GPIO_OUT_LOW)			/* Enable Trackpad */
GPIO(WLAN_OFF_L,		PIN(7, 2), GPIO_OUT_LOW)			/* Disable WLAN */
GPIO(PP3300_DX_WLAN,		PIN(A, 7), GPIO_OUT_LOW)			/* Enable WLAN 3.3V Power */
GPIO(CHARGER_RST_ODL,		PIN(0, 1), GPIO_INPUT | GPIO_PULL_UP)		/* CHARGER_RST_ODL, no-connect */
GPIO(CPU_PROCHOT,		PIN(8, 1), GPIO_OUT_LOW)			/* PROCHOT to SOC */
GPIO(PCH_ACOK,			PIN(5, 0), GPIO_ODR_LOW)			/* ACOK to SOC */
GPIO(PCH_WAKE_L,		PIN(A, 3), GPIO_ODR_HIGH)			/* Wake SOC */
GPIO(PCH_RSMRST_L,		PIN(7, 0), GPIO_OUT_LOW)			/* RSMRST# to SOC */
GPIO(PCH_PWRBTN_L,		PIN(4, 1), GPIO_ODR_HIGH)			/* Power Button to SOC */
GPIO(EC_PLATFORM_RST,		PIN(A, 6), GPIO_OUT_LOW)			/* EC Reset to LDO_EN */
GPIO(SYS_RESET_L,		PIN(6, 1), GPIO_ODR_HIGH)			/* Cold Reset to SOC */
GPIO(PMIC_SLP_SUS_L,		PIN(8, 5), GPIO_OUT_LOW)			/* SLP_SUS# to PMIC */
GPIO(BATTERY_PRESENT_L,		PIN(3, 4), GPIO_INPUT)				/* Battery Present */
GPIO(CCD_MODE_ODL,		PIN(6, 3), GPIO_INPUT)				/* Case Closed Debug Mode */
GPIO(EC_HAVEN_RESET_ODL,	PIN(0, 2), GPIO_INPUT | GPIO_PULL_UP)			/* H1 Reset, no-connect */
GPIO(ENTERING_RW,		PIN(7, 6), GPIO_OUTPUT)				/* EC Entering RW */
GPIO(PMIC_INT_L,		PIN(6, 0), GPIO_INPUT)				/* PMIC interrupt */

/* I2C pins - these will be reconfigured for alternate function below */
GPIO(I2C0_0_SCL,		PIN(B, 5), GPIO_INPUT)				/* EC_I2C00_USB_C0_SCL */
GPIO(I2C0_0_SDA,		PIN(B, 4), GPIO_INPUT)				/* EC_I2C00_USB_C0_SDA */
GPIO(I2C0_1_SCL,		PIN(B, 3), GPIO_INPUT)				/* EC_I2C01_USB_C1_SCL */
GPIO(I2C0_1_SDA,		PIN(B, 2), GPIO_INPUT)				/* EC_I2C01_USB_C1_SDA */
GPIO(I2C1_SCL,			PIN(9, 0), GPIO_INPUT)				/* EC_I2C1_GYRO_SCL */
GPIO(I2C1_SDA,			PIN(8, 7), GPIO_INPUT)				/* EC_I2C1_GYRO_SDA */
GPIO(I2C2_SCL,			PIN(9, 2), GPIO_INPUT)				/* EC_I2C2_SENSOR_3V3_SCL */
GPIO(I2C2_SDA,			PIN(9, 1), GPIO_INPUT)				/* EC_I2C2_SENSOR_3V3_SDA */
GPIO(I2C3_SCL,			PIN(D, 1), GPIO_INPUT)				/* EC_I2C3_POWER_SCL */
GPIO(I2C3_SDA,			PIN(D, 0), GPIO_INPUT)				/* EC_I2C3_POWER_SDA */

/*
 * For P1 and prior: 5V enables: INPUT=1.5A, OUT_LOW=OFF, OUT_HIGH=3A
 * For P1B and later: 5V enables: OUT_LOW=VBUS Off, OUT_HIGH=VBUS On
 */
GPIO(USB_C0_5V_EN,		PIN(4, 2), GPIO_OUT_LOW | GPIO_PULL_UP)		/* C0 5V Enable */
GPIO(USB_C1_5V_EN,		PIN(B, 1), GPIO_OUT_LOW | GPIO_PULL_UP)		/* C1 5V Enable */
GPIO(EN_USB_C0_3A,		PIN(6, 6), GPIO_OUT_LOW)			/* 1.5/3.0 C0 current limit selection */
GPIO(EN_USB_C1_3A,		PIN(3, 5), GPIO_OUT_LOW)			/* 1.5/3.0 C1 current limit selection */
GPIO(USB_C0_PD_RST_L,		PIN(0, 3), GPIO_OUT_LOW)			/* C0 PD Reset */
GPIO(USB_C1_PD_RST_L,		PIN(7, 4), GPIO_OUT_LOW)			/* C1 PD Reset */
GPIO(USB_C0_DP_HPD,		PIN(9, 4), GPIO_INPUT)				/* C0 DP Hotplug Detect */
GPIO(USB_C1_DP_HPD,		PIN(A, 5), GPIO_INPUT)				/* C1 DP Hotplug Detect */
GPIO(USB_C0_TCPC_PWR,		PIN(8, 4), GPIO_OUT_LOW)			/* Enable C0 TCPC Power */
GPIO(USB_C1_TCPC_PWR,		PIN(0, 0), GPIO_OUT_LOW)			/* Enable C1 TCPC Power */
GPIO(USB2_OTG_ID,		PIN(A, 1), GPIO_OUT_LOW)			/* OTG ID */
GPIO(USB2_OTG_VBUSSENSE,	PIN(9, 5), GPIO_OUT_LOW)			/* OTG VBUS Sense */

/* Board ID */
GPIO(BOARD_VERSION1,		PIN(4, 3), GPIO_INPUT | GPIO_PULL_UP)		/* Board ID bit0 */
GPIO(BOARD_VERSION2,		PIN(4, 4), GPIO_INPUT | GPIO_PULL_UP)		/* Board ID bit1 */
GPIO(BOARD_VERSION3,		PIN(4, 5), GPIO_INPUT | GPIO_PULL_UP)		/* Board ID bit2 */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(6, 0x30), 1, MODULE_UART, 0)		/* GPIO64-65 */		/* UART from EC to Servo */
ALTERNATE(PIN_MASK(8, 0x80), 1, MODULE_I2C, 0)		/* GPIO87 */		/* EC_I2C1_GYRO_SDA */
ALTERNATE(PIN_MASK(9, 0x01), 1, MODULE_I2C, 0)		/* GPIO90 */		/* EC_I2C1_GYRO_SCL */
ALTERNATE(PIN_MASK(9, 0x06), 1, MODULE_I2C, 0)		/* GPIO91-92 */		/* EC_I2C2_SENSOR_3V3_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x30), 1, MODULE_I2C, 0)		/* GPIOB4-B5 */		/* EC_I2C00_USB_C0_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x0C), 1, MODULE_I2C, 0)		/* GPOPB2-B3 */		/* EC_I2C01_USB_C1_SDA/SCL */
ALTERNATE(PIN_MASK(D, 0x03), 1, MODULE_I2C, 0)		/* GPIOD0-D1 */		/* EC_I2C3_POWER_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x80), 1, MODULE_PWM, 0)		/* GPIOB7 */		/* KBD_BL_PWM */
/* Left LED PWM Channels */
ALTERNATE(PIN_MASK(C, 0x10), 1, MODULE_PWM, 0)		/* GPIOC4 PWM2 Red*/
ALTERNATE(PIN_MASK(B, 0x40), 1, MODULE_PWM, 0)		/* GPOB6  PWM3 Green*/
ALTERNATE(PIN_MASK(8, 0x01), 1, MODULE_PWM, 0)		/* GPIO80 PWM4 Blue*/
/* Right LED PWM Channels */
ALTERNATE(PIN_MASK(C, 0x04), 1, MODULE_PWM, 0)		/* GPIOC2 PWM1 Red*/
ALTERNATE(PIN_MASK(C, 0x08), 1, MODULE_PWM, 0)		/* GPIOC3 PWM0 Green */
ALTERNATE(PIN_MASK(C, 0x01), 1, MODULE_PWM, 0)		/* GPIOC0 PWM6 Blue */

/* Set unused pins as Input+PU */
GPIO(TP_EC_GPIO_57,   PIN(5, 7), GPIO_INPUT | GPIO_PULL_UP)

/* Keyboard pins */
#define GPIO_KB_INPUT		(GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT		(GPIO_ODR_HIGH)
#define GPIO_KB_OUTPUT_COL2	(GPIO_OUT_LOW)

ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(2, 0xfc), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(0, 0xe0), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(1, 0x7f), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
GPIO(KBD_KSO2,			PIN(1, 7), GPIO_KB_OUTPUT_COL2)
