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

/*
 * TODO(b/76023457): Move below 4 signals to virtual wires over eSPI
 */
GPIO(PCH_PLTRST_L,	 PIN(E, 3), GPIO_INPUT)		/* PLT_RST_L: Platform Reset from SoC */
GPIO(SYS_RESET_L,	 PIN(B, 6), GPIO_ODR_HIGH)	/* SYS_RST_ODL */
GPIO(PCH_SMI_L,		 PIN(D, 4), GPIO_OUT_LOW)	/* EC_SMI_R_ODL */
GPIO(PCH_SCI_L,		 PIN(D, 3), GPIO_OUT_LOW)	/* EC_SCI_R_ODL */

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
GPIO(I2C2_SCL,		 PIN(F, 6), GPIO_INPUT)		/* EC_I2C_USBC_MUX_SCL */
GPIO(I2C2_SDA,		 PIN(F, 7), GPIO_INPUT)		/* EC_I2C_USBC_MUX_SDA */
GPIO(I2C4_SCL,		 PIN(E, 0), GPIO_INPUT)		/* EC_I2C_USB_PD_SCL */
GPIO(I2C4_SDA,		 PIN(E, 7), GPIO_INPUT)		/* EC_I2C_USB_PD_SDA */
GPIO(I2C5_SCL,		 PIN(A, 4), GPIO_INPUT)		/* EC_I2C_PROG_SCL */
GPIO(I2C5_SDA,		 PIN(A, 5), GPIO_INPUT)		/* EC_I2C_PROG_SDA */

/* Alternate functions GPIO definitions */
/* Cr50 requires no pull-ups on UART pins. */
ALTERNATE(PIN_MASK(B, 0x03), 0, MODULE_UART, 0)		/* UART from EC to Servo */
ALTERNATE(PIN_MASK(B, 0x18), 0, MODULE_I2C, 0)		/* I2C0 */
ALTERNATE(PIN_MASK(C, 0x06), 0, MODULE_I2C, 0)		/* I2C1 */
ALTERNATE(PIN_MASK(F, 0xC0), 0, MODULE_I2C, 0)		/* I2C2 */
ALTERNATE(PIN_MASK(E, 0x81), 0, MODULE_I2C, 0)		/* I2C4 */
ALTERNATE(PIN_MASK(A, 0x30), 0, MODULE_I2C, 0)		/* I2C5 */
ALTERNATE(PIN_MASK(I, 0x03), 0, MODULE_ADC, 0)		/* ADC0 & ADC1: BRD_ID1 & BRD_ID2 */
ALTERNATE(PIN_MASK(L, 0x03), 0, MODULE_ADC, 0)		/* ADC13 & ADC14: ADC_USB_C0_VBUS & ADC_USB_C1_VBUS */
ALTERNATE(PIN_MASK(A, 0x48), 0, MODULE_PWM, 0)		/* LED_1_L & LED_2_L */

/* TODO(b/76022415): Determine if low power mode really needs interrupt here */
UNIMPLEMENTED(UART1_RX)
