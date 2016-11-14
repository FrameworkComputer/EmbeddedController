/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(TOUCHPAD_INT, PIN(A, 1), GPIO_INT_FALLING | GPIO_PULL_UP, elan_tp_interrupt)

/* Keyboard inputs */
#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

GPIO_INT(KB_IN00, PIN(A, 3),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN01, PIN(A, 2),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN02, PIN(A, 11), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN03, PIN(A, 4),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN04, PIN(A, 5),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN05, PIN(A, 6),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN06, PIN(A, 7),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN07, PIN(A, 8),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

GPIO(KB_OUT00,          PIN(B, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,          PIN(B, 10), GPIO_KB_OUTPUT)
GPIO(KB_OUT02,          PIN(B, 12), GPIO_KB_OUTPUT)
GPIO(KB_OUT03,          PIN(B, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT04,          PIN(B, 11), GPIO_KB_OUTPUT)
GPIO(KB_OUT05,          PIN(D, 14), GPIO_KB_OUTPUT)
GPIO(KB_OUT06,          PIN(D, 13), GPIO_KB_OUTPUT)
GPIO(KB_OUT07,          PIN(D, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT08,          PIN(C, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,          PIN(B, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,          PIN(C, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT11,          PIN(C, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT12,          PIN(D, 5),  GPIO_KB_OUTPUT)

/* I2C pins should be configured as inputs until I2C module is    */
/* initialized. This will avoid driving the lines unintentionally.*/
GPIO(MASTER_I2C_SCL,		PIN(B, 8), GPIO_INPUT)
GPIO(MASTER_I2C_SDA,		PIN(B, 9), GPIO_INPUT)

/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART, 0) /* USART1: PA09/PA10 - Servo stm32 console UART */

ALTERNATE(PIN_MASK(B, 0x0300), 1, MODULE_I2C, 0) /* I2C MASTER:PB8/9 GPIO_ODR_HIGH */
