/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */

/* User Push buttons */
GPIO(SW_N,   M,  0, GPIO_INT_BOTH, button_event)
GPIO(SW_S,   M,  1, GPIO_INT_BOTH, button_event)
GPIO(SW_W,   M,  2, GPIO_INT_BOTH, button_event)
GPIO(SW_E,   M,  3, GPIO_INT_BOTH, button_event)

/* Outputs */

/* User GPIO LEDs */
GPIO(LED_2,  A,  9,  GPIO_OUT_HIGH,  NULL)
GPIO(LED_3,  A, 10,  GPIO_OUT_LOW,   NULL)
GPIO(LED_4,  A, 11,  GPIO_OUT_LOW,  NULL)
GPIO(LED_5,  A, 12,  GPIO_OUT_LOW,  NULL)
GPIO(LED_6,  A, 13,  GPIO_OUT_LOW,  NULL)
/*GPIO(LED_7,  A, 14,  GPIO_OUT_LOW,  NULL)*/

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)

/* Alternate functions */
ALTERNATE(A, 0x0001, PINMUX(UART0_TX), MODULE_UART,   0) /* UART0 TX: DIOA0 */
ALTERNATE(A, 0x0002, PINMUX(UART0_RX), MODULE_UART,   0) /* UART0 RX: DIOA1 */
