/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel GLK-RVP-ITE board-specific configuration */

/*
 * Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first.
 */

/* Power sequencing interrupts */
GPIO_INT(SUSPWRDNACK,	PIN(E, 1), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S0_L,	PIN(F, 0), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S3_L,	PIN(F, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S4_L,	PIN(F, 3), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(RSMRST_L_PGOOD,PIN(G, 6), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(ALL_SYS_PGOOD,	PIN(I, 7), GPIO_INT_BOTH, power_signal_interrupt)

/* Button interrupts */
GPIO_INT(VOLUME_UP_L,	PIN(D, 5), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(VOLUME_DOWN_L,	PIN(D, 6), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(LID_OPEN,	PIN(E, 2), GPIO_INT_BOTH | GPIO_PULL_UP, lid_interrupt)
GPIO_INT(POWER_BUTTON_L,PIN(E, 4), GPIO_INT_BOTH, power_button_interrupt)

GPIO_INT(AC_PRESENT,	PIN(A, 6), GPIO_INT_BOTH, extpower_interrupt)

GPIO_INT(UART1_RX,	PIN(B, 0), GPIO_INT_FALLING, uart_deepsleep_interrupt) /* UART1 RX input */
#ifdef CONFIG_HOSTCMD_ESPI
/* enable 1.8v input of EC's espi_reset pin, and then this pin takes effect. */
GPIO_INT(ESPI_RESET_L, 	 PIN(D, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V, espi_reset_pin_asserted_interrupt) /* eSPI_reset# */
#endif

/* Type-C interrupts */
UNIMPLEMENTED(USB_C0_PD_INT_ODL)
UNIMPLEMENTED(USB_C1_PD_INT_ODL)

UNIMPLEMENTED(WP_L)
UNIMPLEMENTED(PP3300_PG)
UNIMPLEMENTED(PP5000_PG)

/* Power sequencing GPIOs */
UNIMPLEMENTED(SYS_RESET_L)
GPIO(PCH_RSMRST_L,	PIN(C, 6), GPIO_OUT_LOW)
GPIO(PCH_PWRBTN_L,	PIN(D, 0), GPIO_ODR_HIGH)
GPIO(PCH_SYS_PWROK,	PIN(K, 4), GPIO_OUT_LOW)
GPIO(SMC_SHUTDOWN,	PIN(K, 5), GPIO_OUT_LOW | GPIO_PULL_DOWN)
/*
 * PCH_PROCHOT_ODL is primarily for monitoring the PROCHOT# signal which is
 * normally driven by the PMIC. The EC can also drive this signal in the event
 * that the ambient or charger temperature sensors exceeds their thresholds.
 */
GPIO(CPU_PROCHOT,	PIN(E, 5), GPIO_INPUT)		/* PCH_PROCHOT_ODL */

/* Host communication GPIOs */
GPIO(PCH_WAKE_L,	PIN(L, 0), GPIO_ODR_HIGH)
GPIO(PCH_PLTRST_L,	PIN(E, 3), GPIO_INPUT | GPIO_PULL_UP)

GPIO(DC_JACK_PRESENT_L,	PIN(C, 0), GPIO_INPUT)		/* DC Jack presence coming from +V3P3_A_KBC */
GPIO(USBC_LDO_ENABLE,	PIN(K, 0), GPIO_OUT_HIGH)	/* USB TCPC to enable LDO in dead battery */
UNIMPLEMENTED(ENABLE_BACKLIGHT)
GPIO(ENTERING_RW,	PIN(C, 5), GPIO_OUTPUT)		/* EC_ENTERING_RW */

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C_A_SCL,	PIN(B, 3), GPIO_INPUT)
GPIO(I2C_A_SDA,	PIN(B, 4), GPIO_INPUT)
GPIO(I2C_B_SCL,	PIN(C, 1), GPIO_INPUT)
GPIO(I2C_B_SDA,	PIN(C, 2), GPIO_INPUT)
#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
GPIO(I2C_C_SCL,      PIN(C, 7), GPIO_INPUT)
#else
GPIO(I2C_C_SCL,	PIN(F, 6), GPIO_INPUT)
#endif
GPIO(I2C_C_SDA,	PIN(F, 7), GPIO_INPUT)
GPIO(I2C_E_SCL,	PIN(E, 0), GPIO_INPUT)
GPIO(I2C_E_SDA,	PIN(E, 7), GPIO_INPUT)

/* LPC / eSPI signals */
#if 0
GPIO(LPC_ESPI_RST,	PIN(D, 2), GPIO_INPUT)
GPIO(LAD_EIO_0,		PIN(M, 0), GPIO_INPUT)
GPIO(LAD_EIO_1,		PIN(M, 1), GPIO_INPUT)
GPIO(LAD_EIO_2,		PIN(M, 2), GPIO_INPUT)
GPIO(LAD_EIO_3,		PIN(M, 3), GPIO_INPUT)
GPIO(LPC_ESPI_CLK,	PIN(M, 4), GPIO_INPUT)
GPIO(LFRAME_ESPI_CS,	PIN(M, 5), GPIO_INPUT)
GPIO(SERIRQ_ALERT,	PIN(M, 6), GPIO_INPUT)
#endif

/* Unused pins 3.3V & Interruptable */

/* Unused pins: VSPI 3.3V or 1.8V & Interruptable */

/* Unused pins 3.3V & Non-Interruptable */

/* eSPI: VHIF Unused pins 1.8V & Interruptable */

/* eSPI: VHIF Unused pins 1.8V & Non-Interruptable */

/* Alternate pins for UART */
ALTERNATE(PIN_MASK(B, 0x03), 1, MODULE_UART, GPIO_PULL_UP) /* UART1 */

/* Alternate pins for I2C */
ALTERNATE(PIN_MASK(B, 0x18), 1, MODULE_I2C, 0)	/* I2C A SCL/SDA B3/B4 */
ALTERNATE(PIN_MASK(C, 0x06), 1, MODULE_I2C, 0)	/* I2C B SCL/SDA C1/C2 */
#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
ALTERNATE(PIN_MASK(C, 0x80), 1, MODULE_I2C, 0)	/* I2C C SCL C7 */
#else
ALTERNATE(PIN_MASK(F, 0x40), 1, MODULE_I2C, 0)	/* I2C C SCL F6 */
#endif
ALTERNATE(PIN_MASK(F, 0x80), 1, MODULE_I2C, 0)	/* I2C C SDA F7 */
ALTERNATE(PIN_MASK(E, 0x81), 1, MODULE_I2C, 0)	/* I2C E SCL/SDA E0/E7 */
