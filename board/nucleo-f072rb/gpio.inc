/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(USER_BUTTON, PIN(C, 13), GPIO_INT_FALLING, button_event)

/* Outputs */
GPIO(LED_U, PIN(A, 5), GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x000C), 1, MODULE_UART,  0) /* USART2: PA2/PA3 */

#ifdef CTS_MODULE
/* CTS Signals */
GPIO(HANDSHAKE_INPUT, PIN(A, 4), GPIO_INPUT | GPIO_PULL_UP)
GPIO(HANDSHAKE_OUTPUT, PIN(B, 0), GPIO_ODR_LOW)
GPIO(INPUT_TEST, PIN(C, 1), GPIO_INPUT | GPIO_PULL_UP)
GPIO(OUTPUT_TEST, PIN(C, 0), GPIO_ODR_LOW)
#endif
