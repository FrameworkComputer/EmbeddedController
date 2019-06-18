/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(DP_HPD,       PIN(A, 0),  GPIO_INT_BOTH, hpd_event)

GPIO(USB_C_CC1_PD,     PIN(A, 1),  GPIO_ANALOG)
GPIO(STM_READY,        PIN(A, 2),  GPIO_OUT_LOW) /* factory test only */
GPIO(MCDP_RESET_L,     PIN(A, 3),  GPIO_OUT_HIGH)
GPIO(PD_DAC_REF,       PIN(A, 4),  GPIO_ANALOG)

GPIO(MCDP_READY,       PIN(A, 7),  GPIO_OUT_LOW) /* factory test only */
GPIO(PD_SBU_ENABLE,    PIN(A, 8),  GPIO_OUT_LOW)
GPIO(USB_DM,           PIN(A, 11), GPIO_ANALOG)
GPIO(USB_DP,           PIN(A, 12), GPIO_ANALOG)
GPIO(PD_CC1_TX_EN,     PIN(A, 15), GPIO_OUT_LOW)

GPIO(MCDP_GPIO1,       PIN(B, 0),  GPIO_INPUT)
GPIO(MCDP_CONFIG1,     PIN(B, 1),  GPIO_INPUT)
GPIO(PD_MCDP_SPI_WP_L, PIN(B, 2),  GPIO_OUT_LOW)
GPIO(PD_CC1_TX_DATA,   PIN(B, 4),  GPIO_OUT_LOW)
GPIO(PD_MCDP_SPI_CS_L, PIN(B, 12), GPIO_INPUT)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(B, 0x0008), 0, MODULE_USB_PD, 0)            /* SPI1: SCK(PB3) */
ALTERNATE(PIN_MASK(B, 0x0200), 2, MODULE_USB_PD, 0)            /* TIM17_CH1: PB9 */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   GPIO_PULL_UP) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x0C00), 4, MODULE_UART,   GPIO_PULL_UP) /* USART3: PB10/PB11 */
