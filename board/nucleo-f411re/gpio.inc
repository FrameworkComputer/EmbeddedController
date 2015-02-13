/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(USER_BUTTON_L,   PIN(C, 13), GPIO_INT_BOTH, user_button_evt)

/* User LED */
GPIO(USER_LED,            PIN(A,  5), GPIO_OUT_LOW)

GPIO(BMI160_INT2_L,       PIN(C, 10), GPIO_OUT_LOW)
/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,      PIN(B, 10), GPIO_INPUT)
GPIO(MASTER_I2C_SDA,      PIN(B,  3), GPIO_INPUT)
GPIO(SLAVE_I2C_SCL,       PIN(B,  8), GPIO_INPUT)
GPIO(SLAVE_I2C_SDA,       PIN(B,  9), GPIO_INPUT)

UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x000C), GPIO_ALT_USART,  MODULE_UART, GPIO_PULL_UP) /* USART2: PA2/PA3 */
ALTERNATE(PIN_MASK(B, 0x0400), GPIO_ALT_I2C,    MODULE_I2C,  0) /* I2C MASTER:PB10 */
ALTERNATE(PIN_MASK(B, 0x0008), GPIO_ALT_I2C_23, MODULE_I2C,  0) /* I2C MASTER:PB3 */
ALTERNATE(PIN_MASK(B, 0x0200), GPIO_ALT_I2C,    MODULE_I2C,  0) /* I2C SLAVE:PB9 */
ALTERNATE(PIN_MASK(B, 0x0100), GPIO_ALT_I2C,    MODULE_I2C,  0) /* I2C SLAVE:PB8 */
