/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */
#ifdef CTS_MODULE
#ifndef CTS_MODULE_GPIO
/* Overload C10 for notification. Enabled only for non-GPIO suites as
 * GPIO tests don't require a separate notification line. */
GPIO_INT(CTS_NOTIFY, PIN(C, 10), GPIO_INT_FALLING | GPIO_PULL_UP , cts_irq)
#endif
#endif

/* Outputs */
GPIO(LED_GREEN,   PIN(B, 2), GPIO_OUT_LOW)
GPIO(LED_RED,    PIN(C, 1), GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(B, 0xC0), GPIO_ALT_F7, MODULE_UART,  0) /* USART1: PB6/7 */
ALTERNATE(PIN_MASK(G, 0x0180), GPIO_ALT_F8, MODULE_UART,  0) /* LPUART: PG7/8 */

#ifdef CTS_MODULE
/* CTS Signals */
GPIO(HANDSHAKE_OUTPUT, PIN(A, 9), GPIO_ODR_LOW)
GPIO(HANDSHAKE_INPUT, PIN(A, 8), GPIO_INPUT | GPIO_PULL_UP)
GPIO(OUTPUT_TEST, PIN(C, 11), GPIO_ODR_LOW)
GPIO(CTS_IRQ2, PIN(C, 12), GPIO_ODR_LOW)
#ifdef CTS_MODULE_GPIO
GPIO(INPUT_TEST, PIN(C, 10), GPIO_INPUT | GPIO_PULL_UP)
#endif

GPIO(I2C2_SCL, PIN(B, 10), GPIO_ODR_HIGH) /* I2C port 2 SCL */
GPIO(I2C2_SDA, PIN(B, 11), GPIO_ODR_HIGH) /* I2C port 2 SDA */

ALTERNATE(PIN_MASK(B, 0x0C00), GPIO_ALT_F4, MODULE_I2C,  GPIO_ODR_HIGH) /* I2C2: PB10/11 */
#endif