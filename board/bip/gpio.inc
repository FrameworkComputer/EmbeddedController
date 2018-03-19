/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(LID_OPEN,	 PIN(E, 2), GPIO_INT_BOTH |
				    GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(WP_L,		 PIN(I, 4), GPIO_INT_BOTH, switch_interrupt)		/* EC_WP_ODL */
GPIO_INT(POWER_BUTTON_L, PIN(E, 4), GPIO_INT_BOTH, power_button_interrupt)	/* MECH_PWR_BTN_ODL */

/*
 * TODO(b/76023457): Move below 4 signals to virtual wires over eSPI
 */
GPIO(PCH_PLTRST_L,	 PIN(E, 3), GPIO_INPUT)		/* PLT_RST_L: Platform Reset from SoC */
GPIO(PCH_RCIN_L,	 PIN(B, 6), GPIO_ODR_HIGH)	/* SYS_RST_ODL */
GPIO(PCH_SMI_L,		 PIN(D, 4), GPIO_OUT_LOW)	/* EC_SMI_R_ODL */
GPIO(PCH_SCI_L,		 PIN(D, 3), GPIO_OUT_LOW)	/* EC_SCI_R_ODL */

GPIO(ENTERING_RW,	 PIN(C, 5), GPIO_OUT_LOW)	/* EC_ENTERING_RW */
GPIO(PCH_WAKE_L,	 PIN(D, 1), GPIO_ODR_HIGH)	/* EC_PCH_WAKE_ODL */

/* TODO(b/76022415): Determine if low power mode really needs interrupt here */
UNIMPLEMENTED(UART1_RX)