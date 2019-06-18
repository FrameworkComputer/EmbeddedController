/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(USER_BUTTON, PIN(A, 0), GPIO_INT_FALLING, button_event)

/* Outputs */
GPIO(LED_U, PIN(C, 6), GPIO_OUT_LOW)
GPIO(LED_D, PIN(C, 7), GPIO_OUT_LOW)
GPIO(LED_L, PIN(C, 8), GPIO_OUT_LOW)
GPIO(LED_R, PIN(C, 9), GPIO_OUT_LOW)

/* Flash SPI interface */
GPIO(SPI_WP,   PIN(C,  3), GPIO_OUT_HIGH)
GPIO(SPI_HOLD, PIN(C,  4), GPIO_OUT_HIGH)
GPIO(SPI_CS,   PIN(B, 12), GPIO_OUT_HIGH)

ALTERNATE(PIN_MASK(B, 0xE000), 0, MODULE_SPI_FLASH, 0)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_USART, 0) /* USART1: PA09/PA10 */
ALTERNATE(PIN_MASK(A, 0xC000), 1, MODULE_UART,  0) /* USART2: PA14/PA15 */
ALTERNATE(PIN_MASK(B, 0x0C00), 4, MODULE_USART, 0) /* USART3: PB10/PB11 */
ALTERNATE(PIN_MASK(C, 0x0C00), 0, MODULE_USART, 0) /* USART4: PC10/PC11 */
