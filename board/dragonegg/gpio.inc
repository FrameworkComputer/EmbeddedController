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
