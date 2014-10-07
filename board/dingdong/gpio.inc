/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
GPIO(DP_HPD,            A, 0,  GPIO_INPUT,    NULL)
GPIO(USB_C_CC1_PD,      A, 1,  GPIO_ANALOG,   NULL)
GPIO(PD_DAC_REF,        A, 4,  GPIO_ANALOG,   NULL)
GPIO(DP_AUX_N,          A, 5,  GPIO_INPUT,    NULL)
GPIO(DP_AUX_P,          A, 6,  GPIO_INPUT,    NULL)

GPIO(PD_SBU_ENABLE,     A, 8,  GPIO_OUT_LOW,  NULL)
GPIO(USB_DM,            A, 11, GPIO_ANALOG,   NULL)
GPIO(USB_DP,            A, 12, GPIO_ANALOG,   NULL)
GPIO(PD_CC1_TX_EN,      A, 15, GPIO_OUT_LOW,  NULL)

GPIO(PD_DPSINK_PRESENT, B, 0,  GPIO_INPUT,    NULL)
GPIO(PD_CC1_TX_DATA,    B, 4,  GPIO_OUT_LOW,  NULL)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(B, 0x0008, 0, MODULE_USB_PD, 0)            /* SPI1: SCK(PB3) */
ALTERNATE(B, 0x0200, 2, MODULE_USB_PD, 0)            /* TIM17_CH1: PB9 */
ALTERNATE(A, 0x0600, 1, MODULE_UART,   GPIO_PULL_UP) /* USART1: PA9/PA10 */
