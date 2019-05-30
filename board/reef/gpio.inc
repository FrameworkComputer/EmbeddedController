/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(CHARGER_INT_L,		PIN(3, 3), GPIO_INT_FALLING, bd9995x_vbus_interrupt)		/* CHARGER_EC_INT_ODL from BD99956 */
/*
 * TODO: The pull ups for Parade TCPC interrupt line can be removed in versions
 * of board following EVT in which daughter card (which has an external pull up)
 * will always be inserted.
 */
GPIO_INT(USB_C0_PD_INT_ODL,	PIN(3, 7), GPIO_INT_FALLING, tcpc_alert_event)			/* from Analogix TCPC */
GPIO_INT(USB_C1_PD_INT_ODL,	PIN(B, 1), GPIO_INT_FALLING | GPIO_PULL_UP, tcpc_alert_event)	/* from Parade TCPC */

GPIO_INT(USB_C0_CABLE_DET,	PIN(C, 5), GPIO_INT_RISING, anx74xx_cable_det_interrupt)	/* CABLE_DET from ANX3429 */

#ifdef CONFIG_POWER_S0IX
GPIO_INT(PCH_SLP_S0_L,		PIN(7, 5), GPIO_INT_BOTH, power_signal_interrupt) /* SLP_S0_L */
#endif
GPIO_INT(PCH_SLP_S4_L,		PIN(8, 6), GPIO_INT_BOTH, power_signal_interrupt)		/* SLP_S4_L */
GPIO_INT(PCH_SLP_S3_L,		PIN(7, 3), GPIO_INT_BOTH, power_signal_interrupt)		/* SLP_S3_L */
GPIO_INT(SUSPWRDNACK,		PIN(7, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(RSMRST_L_PGOOD,	PIN(6, 0), GPIO_INT_BOTH, power_signal_interrupt)		/* PMIC_EC_RSMRST_ODL */
GPIO_INT(ALL_SYS_PGOOD,		PIN(5, 0), GPIO_INT_BOTH, power_signal_interrupt)		/* PMIC_EC_PWROK_OD */

GPIO_INT(AC_PRESENT,		PIN(C, 1), GPIO_INT_BOTH, extpower_interrupt)			/* ACOK_OD from BD99956 */
/* TODO: We might remove external pull-up for POWER_BUTTON_L in EVT */
GPIO_INT(POWER_BUTTON_L,	PIN(0, 4), GPIO_INT_BOTH, power_button_interrupt)		/* MECH_PWR_BTN_ODL */
GPIO_INT(LID_OPEN,		PIN(6, 7), GPIO_INT_BOTH, lid_interrupt)
/* Volume up and down buttons need to be swapped. The one closer to the hinge
 * should be volume up and the one closer to the user should be volume down.
 * (cros.bug/p/60057) */
GPIO_INT(EC_VOLDN_BTN_ODL_SWAPPED,	PIN(8, 3), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(EC_VOLUP_BTN_ODL_SWAPPED,	PIN(8, 2), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
#define GPIO_EC_VOLDN_BTN_ODL GPIO_EC_VOLUP_BTN_ODL_SWAPPED
#define GPIO_EC_VOLUP_BTN_ODL GPIO_EC_VOLDN_BTN_ODL_SWAPPED
/* Tablet switch is active-low. L: lid is attached (360 position) H: detached */
GPIO_INT(TABLET_MODE_L,		PIN(3, 6), GPIO_INT_BOTH, tablet_mode_interrupt)

GPIO_INT(WP_L,			PIN(4, 0), GPIO_INT_BOTH | GPIO_SEL_1P8V, switch_interrupt)	/* EC_WP_ODL */

GPIO_INT(BASE_SIXAXIS_INT_L,	PIN(9, 3), GPIO_INT_FALLING | GPIO_SEL_1P8V,
	 bmi160_interrupt)
GPIO(LID_ACCEL_INT_L,		PIN(C, 7), GPIO_INPUT | GPIO_SEL_1P8V)

/* I2C GPIOs will be set to alt. function later. */
GPIO(EC_I2C_GYRO_SDA,		PIN(8, 7), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_GYRO_SCL,		PIN(9, 0), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_SENSOR_SDA,		PIN(9, 1), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_SENSOR_SCL,		PIN(9, 2), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_USB_C0_PD_SDA,	PIN(B, 4), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_PD_SCL,	PIN(B, 5), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_PD_SDA,	PIN(B, 2), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_PD_SCL,	PIN(B, 3), GPIO_INPUT)
GPIO(EC_I2C_POWER_SDA,		PIN(D, 0), GPIO_INPUT)
GPIO(EC_I2C_POWER_SCL,		PIN(D, 1), GPIO_INPUT)

/*
 * LPC:
 * Pins 46, 47, 51, 52, 53, 54, 55, default to LPC mode.
 * Pin 56 (CLKRUN#) defaults to GPIO mode.
 * Pin 57 (SER_IRQ) defaults to LPC mode, but we also have EC_PCH_KB_INT_ODL
 * (Pin B0) in case it doesn't work (Set CONFIG_KEYBOARD_IRQ_GPIO in this case).
 *
 * See also the NO_LPC_ESPI bit in DEVALT1 and the CONFIG_HOSTCMD_SPS option.
 */

GPIO(PCH_SMI_L,			PIN(A, 6), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_SMI_ODL */
GPIO(PCH_SCI_L,			PIN(A, 7), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_SCI_ODL */
#ifndef CONFIG_POWER_S0IX
GPIO(PCH_SLP_S0_L,              PIN(7, 5), GPIO_INPUT)				/* SLP_S0_L */
#endif

/*
 * BRD_ID1 is a an ADC pin which will be used to measure multiple values.
 * Assert EC_BRD_ID_EN_ODL and then read BRD_ID1.
 */
ALTERNATE(PIN_MASK(4, 0x08), 1, MODULE_ADC, 0)
GPIO(EC_BRD_ID_EN_ODL,		PIN(3, 5), GPIO_INPUT)

GPIO(CCD_MODE_ODL,		PIN(6, 3), GPIO_INPUT)
GPIO(EC_HAVEN_RESET_ODL,	PIN(0, 2), GPIO_ODR_HIGH)
GPIO(ENTERING_RW,		PIN(7, 6), GPIO_OUTPUT)			/* EC_ENTERING_RW */

GPIO(PCH_RSMRST_L,		PIN(7, 0), GPIO_OUT_LOW)
GPIO(EC_BATT_PRES_L,		PIN(3, 4), GPIO_INPUT)
GPIO(PMIC_EN,			PIN(8, 5), GPIO_OUT_LOW)
GPIO(EN_PP3300,			PIN(C, 2), GPIO_OUT_LOW)
GPIO(PP3300_PG,			PIN(6, 2), GPIO_INPUT)
GPIO(EN_PP5000,			PIN(C, 6), GPIO_OUT_LOW)
GPIO(PP5000_PG,			PIN(7, 1), GPIO_INPUT)
GPIO(EN_P3300_TRACKPAD_ODL,	PIN(3, 2), GPIO_ODR_LOW)
/* Control the gate for trackpad IRQ. High closes the gate.
 * This is always set low so that the OS can manage the trackpad. */
GPIO(TRACKPAD_INT_GATE,		PIN(A, 1), GPIO_OUT_LOW)
GPIO(PCH_SYS_PWROK,		PIN(E, 7), GPIO_OUT_LOW)		/* EC_PCH_PWROK */
GPIO(ENABLE_BACKLIGHT,		PIN(9, 7), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_BL_EN_OD */

GPIO(WIRELESS_GPIO_WLAN_POWER,	PIN(6, 6), GPIO_ODR_HIGH)		/* EN_PP3300_WLAN_ODL */

/*
 * PCH_PROCHOT_ODL is primarily for monitoring the PROCHOT# signal which is
 * normally driven by the PMIC. The EC can also drive this signal in the event
 * that the ambient or charger temperature sensors exceeds their thresholds.
 */
GPIO(CPU_PROCHOT,		PIN(A, 3), GPIO_INPUT | GPIO_SEL_1P8V)	/* PCH_PROCHOT_ODL */

GPIO(PCH_PWRBTN_L,		PIN(0, 1), GPIO_ODR_HIGH)		/* EC_PCH_PWR_BTN_ODL */
GPIO(PCH_WAKE_L,		PIN(8, 1), GPIO_ODR_HIGH)		/* EC_PCH_WAKE_ODL */
GPIO(USB_C0_HPD_1P8_ODL,	PIN(9, 4), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(USB_C1_HPD_1P8_ODL,	PIN(A, 5), GPIO_INPUT | GPIO_SEL_1P8V)

GPIO(USB2_OTG_VBUSSENSE,	PIN(9, 5), GPIO_OUTPUT)

/* EC_PCH_RTCRST is a sledgehammer for resetting SoC state and should rarely
 * be used. Set as input for now, we'll set it as an output when we want to use
 * it. Has external pull-down resistor. */
GPIO(EC_PCH_RTCRST,		PIN(B, 7), GPIO_INPUT)
GPIO(SYS_RESET_L,		PIN(6, 1), GPIO_ODR_HIGH)		/* SYS_RST_ODL */

/* FIXME: What, if anything, to do about EC_RST_ODL on VCC1_RST#? */

GPIO(CHARGER_RST_ODL,		PIN(C, 0), GPIO_ODR_HIGH)
GPIO(USB_A_CHARGE_EN_L,		PIN(4, 2), GPIO_OUT_LOW)
GPIO(EN_USB_TCPC_PWR,		PIN(C, 3), GPIO_OUT_LOW)
GPIO(USB1_ENABLE,		PIN(4, 1), GPIO_OUT_LOW)

GPIO(USB_C0_PD_RST_L,		PIN(0, 3), GPIO_OUT_LOW)		/* USB_C0_PD_RST_L */
GPIO(USB_C1_PD_RST_ODL,		PIN(7, 4), GPIO_ODR_LOW)

/*
 * Configure as input to enable @ 1.5A, output-low to turn off, or output-high
 * to enable @ 3A.
 */
GPIO(USB_C0_5V_EN,		PIN(D, 3), GPIO_OUT_LOW | GPIO_PULL_UP)	/* EN_USB_C0_5V_OUT, Enable C0 */
GPIO(USB_C1_5V_EN,		PIN(D, 2), GPIO_OUT_LOW | GPIO_PULL_UP)	/* EN_USB_C1_5V_OUT, Enable C1 */

/* Clear for non-HDI breakout, must be pulled high */
GPIO(NC1,			PIN(0, 0), GPIO_INPUT | GPIO_PULL_UP | GPIO_SEL_1P8V)
GPIO(NC2,			PIN(8, 4), GPIO_INPUT | GPIO_PULL_UP | GPIO_SEL_1P8V)

GPIO(ENG_STRAP,			PIN(B, 6), GPIO_INPUT)

GPIO(BAT_LED_BLUE,		PIN(8,  0), GPIO_OUT_HIGH)
GPIO(BAT_LED_AMBER,		PIN(C,  4), GPIO_OUT_HIGH)

/*
 * Alternate function pins
 */
/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)
#define GPIO_KB_OUTPUT_COL2 (GPIO_OUT_LOW)
ALTERNATE(PIN_MASK(3, 0x03), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(2, 0xfc), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(2, 0x03), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(0, 0xe0), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(1, 0x7f), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
GPIO(KBD_KSO2,			 PIN(1, 7), GPIO_KB_OUTPUT_COL2)

ALTERNATE(PIN(4, 4), 6, MODULE_ADC, 0)		/* TEMP_SENSOR_AMB (FIXME: alt function 6?) */
ALTERNATE(PIN(4, 5), 6, MODULE_ADC, 0)		/* TEMP_SENSOR_CHARGER (FIXME: alt function?) */

ALTERNATE(PIN_MASK(8, 0x80), 1, MODULE_I2C, 0)	/* GPIO87 for EC_I2C_GYRO_SDA */
ALTERNATE(PIN_MASK(9, 0x01), 1, MODULE_I2C, 0)	/* GPIO90 for EC_I2C_GYRO_SCL */
ALTERNATE(PIN_MASK(9, 0x06), 1, MODULE_I2C, 0)	/* GPIO92-91 for EC_I2C_SENSOR_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x30), 1, MODULE_I2C, 0)	/* GPIOB5-B4 for EC_I2C_USB_C0_PD_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x0C), 1, MODULE_I2C, 0)	/* GPOPB3-B2 for EC_I2C_USB_C1_PD_SDA/SCL */
ALTERNATE(PIN_MASK(D, 0x03), 1, MODULE_I2C, 0)	/* GPIOD1-D0 for EC_I2C_POWER_SDA/SCL */

/* FIXME: Make UART RX an interrupt? */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0)	/* UART from EC to Servo */
