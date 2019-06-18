/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(DP_HPD,        PIN(A, 0), GPIO_INT_BOTH, hpd_event)

GPIO(USB_C_CC1_PD,      PIN(A, 1), GPIO_ANALOG)
GPIO(STM_READY,         PIN(A, 2), GPIO_OUT_LOW)  /* factory test only */
GPIO(PD_DAC_REF,        PIN(A, 4), GPIO_ANALOG)
GPIO(DP_AUX_N,          PIN(A, 5), GPIO_INPUT)
GPIO(DP_AUX_P,          PIN(A, 6), GPIO_INPUT)

GPIO(PD_SBU_ENABLE,     PIN(A, 8),  GPIO_OUT_LOW)
GPIO(USB_DM,            PIN(A, 11), GPIO_ANALOG)
GPIO(USB_DP,            PIN(A, 12), GPIO_ANALOG)
GPIO(PD_CC1_TX_EN,      PIN(A, 15), GPIO_OUT_LOW)

GPIO(PD_DPSINK_PRESENT, PIN(B, 0), GPIO_INPUT)
GPIO(PD_CC1_TX_DATA,    PIN(B, 4), GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(B, 0x0008), 0, MODULE_USB_PD, 0)            /* SPI1: SCK(PB3) */
ALTERNATE(PIN_MASK(B, 0x0200), 2, MODULE_USB_PD, 0)            /* TIM17_CH1: PB9 */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   GPIO_PULL_UP) /* USART1: PA9/PA10 */
