/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(LID_OPEN,	 PIN(F, 3), GPIO_INT_BOTH, lid_interrupt)
GPIO_INT(WP_L,		 PIN(F, 1), GPIO_INT_BOTH, switch_interrupt)		/* EC_WP_ODL */
GPIO_INT(POWER_BUTTON_L, PIN(E, 2), GPIO_INT_BOTH, power_button_interrupt)	/* MECH_PWR_BTN_ODL */
#ifdef CONFIG_LOW_POWER_IDLE
/* Used to wake up the EC from Deep Doze mode when writing to console */
GPIO_INT(UART1_RX,	 PIN(B, 0), GPIO_INT_BOTH, uart_deepsleep_interrupt)	/* UART_SERVO_TX_EC_RX */
#endif



#ifdef CONFIG_HOSTCMD_ESPI
/* enable 1.8v input of EC's espi_reset pin, and then this pin takes effect. */
GPIO_INT(ESPI_RESET_L, 	 PIN(D, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V, espi_reset_pin_asserted_interrupt) /* eSPI_reset# */
#endif

GPIO(SYS_RESET_L,	 PIN(D, 1), GPIO_ODR_HIGH)	/* SYS_RST_ODL */
GPIO(ENTERING_RW,	 PIN(G, 0), GPIO_OUT_LOW)	/* EC_ENTERING_RW */
GPIO(PCH_WAKE_L,	 PIN(D, 5), GPIO_ODR_HIGH)	/* EC_PCH_WAKE_ODL */
GPIO(PCH_PWRBTN_L, 	 PIN(B, 6), GPIO_ODR_HIGH)	/* EC_PCH_PWR_BTN_ODL */

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C0_SCL,		 PIN(B, 3), GPIO_INPUT)		/* EC_PROG_SCL */
GPIO(I2C0_SDA,		 PIN(B, 4), GPIO_INPUT)		/* EC_PROG_SDA */
GPIO(I2C1_SCL,		 PIN(C, 1), GPIO_INPUT)		/* EC_I2C_SENSOR_KB_BL_SCL */
GPIO(I2C1_SDA,		 PIN(C, 2), GPIO_INPUT)		/* EC_I2C_SENSOR_KB_BL_SDA */
GPIO(I2C2_SCL,		 PIN(F, 6), GPIO_INPUT)		/* EC_I2C_USB_C1C2_SCL */
GPIO(I2C2_SDA,		 PIN(F, 7), GPIO_INPUT)		/* EC_I2C_USB_C1C2_SDA */
GPIO(I2C4_SCL,		 PIN(E, 0), GPIO_INPUT)		/* EC_I2C_USB_C0_SCL */
GPIO(I2C4_SDA,		 PIN(E, 7), GPIO_INPUT)		/* EC_I2C_USB_C0_SDA */
GPIO(I2C5_SCL,		 PIN(A, 4), GPIO_INPUT)		/* EC_I2C_POWER_SCL */
GPIO(I2C5_SDA,		 PIN(A, 5), GPIO_INPUT)		/* EC_I2C_POWER_SDA */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x18), 0, MODULE_I2C, 0)		/* I2C0 */
ALTERNATE(PIN_MASK(C, 0x06), 0, MODULE_I2C, 0)		/* I2C1 */
ALTERNATE(PIN_MASK(F, 0xC0), 0, MODULE_I2C, 0)		/* I2C2 */
ALTERNATE(PIN_MASK(E, 0x81), 0, MODULE_I2C, 0)		/* I2C4 */
ALTERNATE(PIN_MASK(A, 0x30), 0, MODULE_I2C, 0)		/* I2C5 */

