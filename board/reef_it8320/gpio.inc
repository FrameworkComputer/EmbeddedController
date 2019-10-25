/* -*- mode:c -*-
 *
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first.
 */
GPIO_INT(CHARGER_INT_L,		PIN(A, 6), GPIO_INT_FALLING, bd9995x_vbus_interrupt)	/* CHARGER_EC_INT_ODL from BD99956 */
GPIO_INT(AC_PRESENT,		PIN(A, 7), GPIO_INT_BOTH, extpower_interrupt)		/* ACOK_OD from BD99956 */
#ifdef CONFIG_LOW_POWER_IDLE
GPIO_INT(UART1_RX,		PIN(B, 0), GPIO_INT_FALLING, uart_deepsleep_interrupt)	/* UART_SERVO_TX_EC_RX */
#endif
GPIO_INT(EC_VOLUP_BTN_ODL,	PIN(D, 5), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)	/* EC_VOLUP_BTN_ODL */
GPIO_INT(EC_VOLDN_BTN_ODL,	PIN(D, 6), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)	/* EC_VOLDN_BTN_ODL */
#ifdef CONFIG_POWER_S0IX
GPIO_INT(PCH_SLP_S0_L,		PIN(B, 7), GPIO_INT_BOTH, power_signal_interrupt)				/* SLP_S0_L */
#endif
GPIO_INT(SUSPWRDNACK,		PIN(E, 1), GPIO_INT_BOTH, power_signal_interrupt)	/* SUSPWRNACK */
GPIO_INT(LID_OPEN,		PIN(E, 2), GPIO_INT_BOTH, lid_interrupt)		/* LID_OPEN */
#ifndef CONFIG_HOSTCMD_ESPI
GPIO_INT(PCH_PLTRST_L,   	PIN(E, 3), GPIO_INT_BOTH | GPIO_PULL_UP,  lpcrst_interrupt)	/* PLT_RST_L */
#endif
GPIO_INT(POWER_BUTTON_L,	PIN(E, 4), GPIO_INT_BOTH, power_button_interrupt)	/* MECH_PWR_BTN_ODL */
GPIO_INT(ALL_SYS_PGOOD,		PIN(F, 0), GPIO_INT_BOTH, power_signal_interrupt)	/* PMIC_EC_PWROK_OD */
GPIO_INT(RSMRST_L_PGOOD,	PIN(F, 1), GPIO_INT_BOTH, power_signal_interrupt)	/* PMIC_EC_RSMRST_ODL */
GPIO_INT(PCH_SLP_S3_L,		PIN(F, 2), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S3_L */
GPIO_INT(PCH_SLP_S4_L,		PIN(F, 3), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S4_L */
GPIO_INT(WP_L,			PIN(I, 4), GPIO_INT_BOTH | GPIO_SEL_1P8V, switch_interrupt)	/* EC_WP_ODL_R */

GPIO(EN_USB_C0_3A,		PIN(A, 0), GPIO_ODR_LOW)			/* 1.5/3.0 C0 current limit selection */
GPIO(EN_USB_C1_3A,		PIN(A, 1), GPIO_ODR_LOW)			/* 1.5/3.0 C1 current limit selection */
GPIO(EN_P3300_TRACKPAD_ODL,	PIN(A, 2), GPIO_ODR_HIGH)			/* EN_PP3300_TRACKPAD_ODL */
GPIO(EC_HAVEN_RESET_ODL,	PIN(A, 3), GPIO_ODR_HIGH)			/* EC_HAVEN_RST_ODL */
/* Pin A.4 A.5 (I2C) for iteflash (servo board) */
GPIO(WIRELESS_GPIO_WLAN_POWER,	PIN(B, 2), GPIO_ODR_HIGH)			/* EN_PP3300_WLAN_ODL */
/* I2C GPIOs will be set to ALT function later. */
GPIO(EC_I2C_A_SCL,		PIN(B, 3), GPIO_INPUT | GPIO_SEL_1P8V)		/* EC_I2C_GYRO_SCL */
GPIO(EC_I2C_A_SDA,		PIN(B, 4), GPIO_INPUT | GPIO_SEL_1P8V)		/* EC_I2C_GYRO_SDA */
GPIO(ENABLE_BACKLIGHT,		PIN(B, 5), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_BL_EN_OD */
GPIO(SYS_RESET_L,		PIN(B, 6), GPIO_ODR_HIGH)			/* SYS_RST_ODL */
#ifndef CONFIG_POWER_S0IX
GPIO(PCH_SLP_S0_L,		PIN(B, 7), GPIO_INPUT)				/* SLP_S0_L */
#endif
GPIO(EC_BATT_PRES_L,		PIN(C, 0), GPIO_INPUT)				/* EC_BATT_PRES_L */
GPIO(EC_I2C_B_SCL,		PIN(C, 1), GPIO_INPUT | GPIO_SEL_1P8V)		/* EC_I2C_SENSOR_U_SCL */
GPIO(EC_I2C_B_SDA,		PIN(C, 2), GPIO_INPUT | GPIO_SEL_1P8V)		/* EC_I2C_SENSOR_U_SDA */
/*
 * BRD_ID1 is a an ADC pin which will be used to measure multiple values.
 * Assert EC_BRD_ID_EN_ODL and then read BRD_ID1.
 */
GPIO(EC_BRD_ID_EN_ODL,		PIN(C, 3), GPIO_INPUT)				/* EC_BRD_ID_EN_ODL */
GPIO(CCD_MODE_ODL,		PIN(C, 4), GPIO_INPUT)				/* CCD_MODE_ODL */
GPIO(ENTERING_RW,		PIN(C, 5), GPIO_OUT_LOW)			/* EC_ENTERING_RW */
GPIO(PCH_RSMRST_L,		PIN(C, 6), GPIO_OUT_LOW)			/* PCH_RSMRST_L */
/*
 * PCH_PROCHOT_ODL is primarily for monitoring the PROCHOT# signal which is
 * normally driven by the PMIC. The EC can also drive this signal in the event
 * that the ambient or charger temperature sensors exceeds their thresholds.
 */
GPIO(CPU_PROCHOT,		PIN(C, 7), GPIO_INPUT | GPIO_SEL_1P8V)		/* PCH_PROCHOT_ODL */
GPIO(PCH_PWRBTN_L,		PIN(D, 0), GPIO_ODR_HIGH)			/* EC_PCH_PWR_BTN_ODL */
GPIO(PCH_WAKE_L,		PIN(D, 1), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_PCH_WAKE_ODL */
GPIO(DP_MUX_EN,			PIN(D, 2), GPIO_OUT_HIGH)			/* DB_MUX_EN */
GPIO(PCH_SCI_L,			PIN(D, 3), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_SCI_ODL */
GPIO(PCH_SMI_L,			PIN(D, 4), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_SMI_ODL */
GPIO(PMIC_EN,			PIN(D, 7), GPIO_OUT_LOW)			/* PMIC_A_RAILS_EN */
GPIO(EC_I2C_E_SCL,		PIN(E, 0), GPIO_INPUT)				/* EC_I2C_POWER_3V3_SCL */
/* FIXME: this pin doesn't support 1.8v */
#if 0
GPIO(KBD_IRQ_L, 		PIN(E, 5), GPIO_ODR_HIGH)			/* EC_PCH_KB_INT_ODL */
#endif
GPIO(CHARGER_RST_ODL,		PIN(E, 6), GPIO_ODR_HIGH)			/* CHARGER_RST_ODL */
GPIO(EC_I2C_E_SDA,		PIN(E, 7), GPIO_INPUT)				/* EC_I2C_POWER_3V3_SDA */
/* F.5 F.4 are cc pins of PD0 */
GPIO(EC_I2C_C_SCL,		PIN(F, 6), GPIO_INPUT)				/* EC_I2C_USBC_MUX_SCL */
GPIO(EC_I2C_C_SDA,		PIN(F, 7), GPIO_INPUT)				/* EC_I2C_USBC_MUX_SDA */
GPIO(LPC_CLKRUN_L,		PIN(H, 0), GPIO_OUT_LOW)			/* LPC_CLKRUN_L */
/* H.1 H.2 are cc pins of PD1 */
GPIO(TRACKPAD_INT_GATE, 	PIN(H, 3), GPIO_OUT_LOW)
GPIO(USB2_OTG_VBUSSENSE,	PIN(H, 4), GPIO_OUT_LOW)
GPIO(USB_C0_HPD_1P8_ODL,	PIN(J, 0), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* USB_C0_HPD_1V8_ODL */
GPIO(USB_C1_HPD_1P8_ODL,	PIN(J, 1), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* USB_C1_HPD_1V8_ODL */
GPIO(LID_ACCEL_INT_L,		PIN(J, 3), GPIO_INPUT | GPIO_SEL_1P8V)		/* LID_ACCEL_INT_L */
/* NOTE: Active low */
GPIO(USB_C0_CC1_VCONN_EN,	PIN(J, 4), GPIO_ODR_HIGH)			/* USB_C0_CC1_VCONN_EN_ODL */
GPIO(USB_C0_CC2_VCONN_EN,	PIN(J, 5), GPIO_ODR_HIGH)			/* USB_C0_CC2_VCONN_EN_ODL */

GPIO(EN_PP3300,			PIN(K, 0), GPIO_OUT_LOW)			/* EN_PP3300 */
GPIO(PP3300_PG,			PIN(K, 1), GPIO_INPUT)				/* PP3300_PG_OD */
GPIO(EN_PP5000,			PIN(K, 2), GPIO_OUT_LOW)			/* EN_PP5000 */
GPIO(PP5000_PG,			PIN(K, 3), GPIO_INPUT)				/* PP5000_PG_OD */
GPIO(PCH_SYS_PWROK,		PIN(K, 4), GPIO_OUT_LOW)			/* EC_PCH_PWROK */
/* NOTE: These two pins are reserved on this test board. */
GPIO(USB_C1_CC1_VCONN_EN,	PIN(K, 5), GPIO_INPUT | GPIO_PULL_DOWN)		/* USB_C1_CC1_VCONN_EN */
GPIO(USB_C1_CC2_VCONN_EN,	PIN(K, 6), GPIO_INPUT | GPIO_PULL_DOWN)		/* USB_C1_CC2_VCONN_EN */
/* EC_PCH_RTCRST is a sledgehammer for resetting SoC state and should rarely
 * be used. Set as input for now, we'll set it as an output when we want to use
 * it. Has external pull-down resistor. */
GPIO(EC_PCH_RTCRST,		PIN(K, 7), GPIO_INPUT)				/* EC_PCH_RTCRST */
GPIO(USB_A_CHARGE_EN_L, 	PIN(L, 0), GPIO_OUT_LOW)			/* USB_A_CHARGE_EN_L */
GPIO(USB1_ENABLE,		PIN(L, 1), GPIO_OUT_LOW)			/* EN_USB_A_5V */
/*
 * Configure as input to enable @ 1.5A, output-low to turn off, or output-high
 * to enable @ 3A.
 */
GPIO(USB_C0_5V_EN,		PIN(L, 2), GPIO_OUT_LOW)		        /* EN_USB_C0_5V_OUT, Enable C0 */
GPIO(USB_C1_5V_EN,		PIN(L, 3), GPIO_OUT_LOW)		        /* EN_USB_C1_5V_OUT, Enable C1 */
GPIO(BAT_LED_BLUE,		PIN(L, 4), GPIO_OUT_HIGH)			/* BLUE_PWR_LED */
GPIO(BAT_LED_AMBER,		PIN(L, 5), GPIO_OUT_HIGH)			/* ORANGE_CHG_LED */
GPIO(USB_C0_DISCHARGE,		PIN(L, 6), GPIO_OUT_LOW)			/* USB_C0_DISCHARGE */
GPIO(USB_C1_DISCHARGE, 		PIN(L, 7), GPIO_OUT_LOW)			/* USB_C1_DISCHARGE */

/*
 * Alternate function pins
 */
ALTERNATE(PIN_MASK(B, 0x03), 1, MODULE_UART, 0)            /* UART1 */
ALTERNATE(PIN_MASK(B, 0x18), 1, MODULE_I2C, 0)             /* I2C A SCL/SDA */
ALTERNATE(PIN_MASK(C, 0x06), 1, MODULE_I2C, 0)             /* I2C B SCL/SDA */
ALTERNATE(PIN_MASK(E, 0x81), 1, MODULE_I2C, 0)             /* I2C E SCL/SDA */
ALTERNATE(PIN_MASK(F, 0xC0), 1, MODULE_I2C, 0)             /* I2C C SCL/SDA */
