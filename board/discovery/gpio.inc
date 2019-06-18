/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(USER_BUTTON, PIN(A, 0), GPIO_INT_BOTH, button_event)

/* Outputs */
GPIO(LED_BLUE,    PIN(B, 6), GPIO_OUT_LOW)
GPIO(LED_GREEN,   PIN(B, 7), GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x0600), GPIO_ALT_USART, MODULE_UART,  0) /* USART1: PA09/PA10 */
ALTERNATE(PIN_MASK(A, 0x000C), GPIO_ALT_USART, MODULE_USART, 0) /* USART2: PA02/PA03 */
ALTERNATE(PIN_MASK(B, 0x0C00), GPIO_ALT_USART, MODULE_USART, 0) /* USART3: PB10/PB11 */
