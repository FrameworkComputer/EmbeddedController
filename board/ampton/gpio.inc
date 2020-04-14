/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(LID_OPEN,	 PIN(E, 2), GPIO_INT_BOTH, lid_interrupt)
GPIO_INT(WP_L,		 PIN(I, 4), GPIO_INT_BOTH, switch_interrupt)		/* EC_WP_ODL */
GPIO_INT(POWER_BUTTON_L, PIN(E, 4), GPIO_INT_BOTH, power_button_interrupt)	/* MECH_PWR_BTN_ODL */
#ifdef CONFIG_LOW_POWER_IDLE
/* Used to wake up the EC from Deep Doze mode when writing to console */
GPIO_INT(UART1_RX,	 PIN(B, 0), GPIO_INT_BOTH, uart_deepsleep_interrupt)	/* UART_SERVO_TX_EC_RX */
#endif

/* USB-C interrupts */
GPIO_INT(USB_C0_PD_INT_ODL,	PIN(H, 6), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C1_PD_INT_ODL,	PIN(H, 5), GPIO_INT_FALLING, ppc_interrupt)

/* Power State interrupts */
#ifdef CONFIG_POWER_S0IX
GPIO_INT(PCH_SLP_S0_L,	 PIN(G, 6), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S0_L */
#endif
GPIO_INT(PCH_SLP_S4_L,	 PIN(F, 3), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S4_L */
GPIO_INT(PCH_SLP_S3_L,	 PIN(F, 2), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S3_L */
GPIO_INT(SUSPWRDNACK,	 PIN(E, 1), GPIO_INT_BOTH, power_signal_interrupt)	/* SUSPWRDNACK */
GPIO_INT(RSMRST_L_PGOOD, PIN(F, 1), GPIO_INT_BOTH, power_signal_interrupt)	/* PMIC_EC_RSMRST_ODL */
GPIO_INT(ALL_SYS_PGOOD,	 PIN(F, 0), GPIO_INT_BOTH, power_signal_interrupt)	/* PMIC_EC_PWROK_OD */
GPIO_INT(AC_PRESENT, 	 PIN(A, 7), GPIO_INT_BOTH, extpower_interrupt)		/* ACOK_OD */

#ifdef CONFIG_HOSTCMD_ESPI
/* enable 1.8v input of EC's espi_reset pin, and then this pin takes effect. */
GPIO_INT(ESPI_RESET_L, 	 PIN(D, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V, espi_reset_pin_asserted_interrupt) /* eSPI_reset# */
#endif

/* Other interrupts */
GPIO_INT(TABLET_MODE_L,		PIN(H, 4), GPIO_INT_BOTH, gmr_tablet_switch_isr)
GPIO_INT(EC_VOLDN_BTN_ODL,	PIN(D, 6), GPIO_INT_BOTH, button_interrupt)
GPIO_INT(EC_VOLUP_BTN_ODL,	PIN(D, 5), GPIO_INT_BOTH, button_interrupt)
GPIO_INT(BASE_SIXAXIS_INT_L,	PIN(J, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V, bmi160_interrupt)
GPIO(LID_ACCEL_INT_L,		PIN(J, 3), GPIO_INPUT | GPIO_SEL_1P8V)

GPIO(PCH_PLTRST_L,	 PIN(E, 3), GPIO_INPUT)		/* PLT_RST_L: Platform Reset from SoC */
GPIO(SYS_RESET_L,	 PIN(B, 6), GPIO_ODR_HIGH)	/* SYS_RST_ODL */
GPIO(PCH_RTCRST,	 PIN(K, 7), GPIO_OUT_LOW)	/* EC_PCH_RTCRST */

GPIO(ENTERING_RW,	 PIN(C, 5), GPIO_OUT_LOW)	/* EC_ENTERING_RW */
GPIO(PCH_WAKE_L,	 PIN(D, 1), GPIO_ODR_HIGH)	/* EC_PCH_WAKE_ODL */
GPIO(PCH_PWRBTN_L, 	 PIN(D, 0), GPIO_ODR_HIGH)	/* EC_PCH_PWR_BTN_ODL */

GPIO(EN_PP5000,		 PIN(K, 2), GPIO_OUT_LOW)	/* EN_PP5000_A */
GPIO(PP5000_PG,		 PIN(K, 3), GPIO_INPUT)		/* PP5000_PG_OD */
GPIO(EN_PP3300,		 PIN(K, 5), GPIO_OUT_LOW)	/* EN_PP3300_A */
GPIO(PP3300_PG,		 PIN(K, 1), GPIO_INPUT)		/* PP3300_PG_OD */
GPIO(PMIC_EN,		 PIN(D, 7), GPIO_OUT_LOW)	/* Enable A Rails via PMIC */
GPIO(PCH_RSMRST_L,	 PIN(C, 6), GPIO_OUT_LOW)	/* RSMRST# to SOC. All _A rails now up. */
GPIO(PCH_SYS_PWROK,	 PIN(K, 4), GPIO_OUT_LOW)	/* EC_PCH_PWROK. All S0 rails now up. */

/* Peripheral rails */
GPIO(ENABLE_BACKLIGHT,		PIN(B, 5), GPIO_ODR_HIGH |
					   GPIO_SEL_1P8V)	/* EC_BL_EN_OD */
GPIO(EN_P3300_TRACKPAD_ODL,	PIN(A, 2), GPIO_ODR_HIGH)

GPIO(EC_BATT_PRES_L, 	 PIN(C, 0), GPIO_INPUT)

/*
 * PCH_PROCHOT_ODL is primarily for monitoring the PROCHOT# signal which is
 * normally driven by the PMIC. The EC can also drive this signal in the event
 * that the ambient or charger temperature sensors exceeds their thresholds.
 */
GPIO(CPU_PROCHOT,	 PIN(C, 7), GPIO_INPUT | GPIO_SEL_1P8V)	/* PCH_PROCHOT_ODL */

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C0_SCL,		 PIN(B, 3), GPIO_INPUT)		/* EC_I2C_POWER_3V3_SCL */
GPIO(I2C0_SDA,		 PIN(B, 4), GPIO_INPUT)		/* EC_I2C_POWER_3V3_SDA */
GPIO(I2C1_SCL,		 PIN(C, 1), GPIO_INPUT |
				    GPIO_SEL_1P8V)	/* EC_I2C_SENSOR_U_SCL */
GPIO(I2C1_SDA,		 PIN(C, 2), GPIO_INPUT |
				    GPIO_SEL_1P8V)	/* EC_I2C_SENSOR_U_SDA */
GPIO(I2C2_SCL,		 PIN(F, 6), GPIO_INPUT)		/* EC_I2C_USB_C0_MUX_SCL */
GPIO(I2C2_SDA,		 PIN(F, 7), GPIO_INPUT)		/* EC_I2C_USB_C0_MUX_SDA */
GPIO(I2C4_SCL,		 PIN(E, 0), GPIO_INPUT)		/* EC_I2C_USB_C1_MUX_SCL */
GPIO(I2C4_SDA,		 PIN(E, 7), GPIO_INPUT)		/* EC_I2C_USB_C1_MUX_SDA */
GPIO(I2C5_SCL,		 PIN(A, 4), GPIO_INPUT)		/* EC_I2C_PROG_SCL */
GPIO(I2C5_SDA,		 PIN(A, 5), GPIO_INPUT)		/* EC_I2C_PROG_SDA */

/* USB pins */
GPIO(EN_USB_A0_5V,	 PIN(B, 7), GPIO_OUT_LOW)	/* Enable A0 5V Charging */
GPIO(EN_USB_A1_5V,	 PIN(H, 3), GPIO_OUT_LOW)	/* Enable A1 5V Charging */
GPIO(USB_A0_CHARGE_EN_L, PIN(K, 0), GPIO_OUT_HIGH)	/* Enable A0 1.5A Charging */
GPIO(USB_A1_CHARGE_EN_L, PIN(K, 6), GPIO_OUT_HIGH)	/* Enable A1 1.5A Charging */
GPIO(USB_C0_HPD_1V8_ODL, PIN(J, 0), GPIO_INPUT |
				    GPIO_SEL_1P8V)	/* C0 DP Hotplug Detect */
GPIO(USB_C1_HPD_1V8_ODL, PIN(J, 1), GPIO_INPUT |
				    GPIO_SEL_1P8V)	/* C1 DP Hotplug Detect */
GPIO(USB_C0_BC12_CHG_DET_L, PIN(A, 0), GPIO_INPUT)	/* C0 BC1.2 Detect */
GPIO(USB_C1_BC12_CHG_DET_L, PIN(I, 0), GPIO_INPUT)	/* C1 BC1.2 Detect */
GPIO(USB_C0_BC12_VBUS_ON,   PIN(J, 4), GPIO_OUT_LOW)	/* C0 BC1.2 Power */
GPIO(USB_C1_BC12_VBUS_ON,   PIN(J, 5), GPIO_OUT_LOW)	/* C1 BC1.2 Power */
GPIO(USB_C0_PD_RST_ODL,     PIN(L, 6), GPIO_ODR_HIGH)	/* C0 PD Reset */
GPIO(USB_C1_PD_RST_ODL,     PIN(L, 7), GPIO_ODR_HIGH)	/* C1 PD Reset */

GPIO(CCD_MODE_ODL,	    PIN(C, 4), GPIO_INPUT)

/* LED */
GPIO(BAT_LED_AMBER,  PIN(A, 6), GPIO_OUT_LOW)		/* LED_1_EC */
GPIO(BAT_LED_WHITE,  PIN(A, 3), GPIO_OUT_LOW)		/* LED_2_EC */

/* MKBP event synchronization */
GPIO(EC_INT_L, PIN(E, 6), GPIO_ODR_HIGH)

GPIO(WFCAM_VSYNC,		PIN(D, 4), GPIO_INPUT | GPIO_PULL_DOWN)

UNIMPLEMENTED(KB_BL_PWR_EN)

/* NC pins, enable internal pull-down to avoid floating state. */
GPIO(GPIOB2_NC,	     PIN(B, 2), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIOG0_NC,	     PIN(G, 0), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIOG1_NC,	     PIN(G, 1), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIOH0_NC,	     PIN(H, 0), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIOL2_NC,	     PIN(L, 2), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIOL3_NC,	     PIN(L, 3), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIOM6_NC,	     PIN(M, 6), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIOD3_NC,      PIN(D, 3), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIOI1_NC,      PIN(I, 1), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(GPIO12_NC,      PIN(I, 2), GPIO_INPUT | GPIO_PULL_DOWN)

/* Alternate functions GPIO definitions */
/* Cr50 requires no pull-ups on UART pins. */
ALTERNATE(PIN_MASK(B, 0x03), 0, MODULE_UART, 0)		/* UART from EC to Servo */
ALTERNATE(PIN_MASK(B, 0x18), 0, MODULE_I2C, 0)		/* I2C0 */
ALTERNATE(PIN_MASK(C, 0x06), 0, MODULE_I2C, GPIO_SEL_1P8V) /* I2C1 - 1.8V */
ALTERNATE(PIN_MASK(F, 0xC0), 0, MODULE_I2C, 0)		/* I2C2 */
ALTERNATE(PIN_MASK(E, 0x81), 0, MODULE_I2C, 0)		/* I2C4 */
ALTERNATE(PIN_MASK(A, 0x30), 0, MODULE_I2C, 0)		/* I2C5 */
ALTERNATE(PIN_MASK(L, 0x03), 0, MODULE_ADC, 0)		/* ADC13 & ADC14: ADC_USB_C0_VBUS & ADC_USB_C1_VBUS */
ALTERNATE(PIN_MASK(I, 0x28), 0, MODULE_ADC, 0)		/* ADC3 & ADC5: TEMP_SENSOR_AMB & TEMP_SENSOR_CHARGER */
