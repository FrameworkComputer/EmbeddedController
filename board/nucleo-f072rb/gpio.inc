/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(USER_BUTTON, PIN(C, 13), GPIO_INT_FALLING, button_event)
#ifdef CTS_MODULE
#ifndef CTS_MODULE_GPIO
/* Overload C1 for interrupt. Enabled only for non-GPIO suites as
 * GPIO tests don't require a separate notification line. */
GPIO_INT(CTS_IRQ1, PIN(C, 1), GPIO_INT_FALLING | GPIO_PULL_UP , cts_irq1)
/* Used to disable interrupt. This IRQ# has to match the number used for the
 * pin set above */
#define CTS_IRQ_NUMBER STM32_IRQ_EXTI0_1
#endif
GPIO_INT(CTS_IRQ2, PIN(C, 2), GPIO_INT_FALLING | GPIO_PULL_UP , cts_irq2)
#endif

/* Outputs */
GPIO(LED_U, PIN(A, 5), GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x000C), 1, MODULE_UART,  0) /* USART2: PA2/PA3 */

GPIO(I2C1_SCL, PIN(B, 6), GPIO_ODR_HIGH) /* I2C port 1 SCL */
GPIO(I2C1_SDA, PIN(B, 7), GPIO_ODR_HIGH) /* I2C port 1 SDA */

/* I2C1: PB6/7*/
ALTERNATE(PIN_MASK(B, 0x00C0), GPIO_ALT_F1, MODULE_I2C, GPIO_PULL_UP)

#ifdef CTS_MODULE
/* CTS Signals */
GPIO(HANDSHAKE_INPUT, PIN(A, 4), GPIO_INPUT | GPIO_PULL_UP)
GPIO(HANDSHAKE_OUTPUT, PIN(B, 0), GPIO_ODR_LOW)
#ifdef CTS_MODULE_GPIO
GPIO(INPUT_TEST, PIN(C, 1), GPIO_INPUT | GPIO_PULL_UP)
#endif
GPIO(OUTPUT_TEST, PIN(C, 0), GPIO_ODR_LOW)
#endif
