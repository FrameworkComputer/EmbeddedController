/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(USER_BUTTON, A, 0, GPIO_INT_FALLING, button_event)

/* Outputs */
GPIO(LED_U, C, 6, GPIO_OUT_LOW)
GPIO(LED_D, C, 7, GPIO_OUT_LOW)
GPIO(LED_L, C, 8, GPIO_OUT_LOW)
GPIO(LED_R, C, 9, GPIO_OUT_LOW)

/* Flash SPI interface */
GPIO(SPI_WP,   C,  3, GPIO_OUT_HIGH)
GPIO(SPI_HOLD, C,  4, GPIO_OUT_HIGH)
GPIO(SPI_CS,   B, 12, GPIO_OUT_HIGH)

ALTERNATE(B, 0xE000, 0, MODULE_SPI_MASTER, 0)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(A, 0x0600, 1, MODULE_USART, 0) /* USART1: PA09/PA10 */
ALTERNATE(A, 0xC000, 1, MODULE_UART,  0) /* USART2: PA14/PA15 */
ALTERNATE(B, 0x0C00, 1, MODULE_USART, 0) /* USART3: PB10/PB11 */
ALTERNATE(C, 0x0C00, 1, MODULE_USART, 0) /* USART4: PC10/PC11 */
