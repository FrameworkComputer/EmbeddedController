/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C pins should be configured as inputs until I2C module is    */
/* initialized. This will avoid driving the lines unintentionally.*/
GPIO(MASTER_I2C_SCL,		PIN(B, 8), GPIO_INPUT)
GPIO(MASTER_I2C_SDA,		PIN(B, 9), GPIO_INPUT)

/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART, 0) /* USART1: PA09/PA10 - Servo stm32 console UART */

ALTERNATE(PIN_MASK(B, 0x0300), 1, MODULE_I2C, 0) /* I2C MASTER:PB8/9 GPIO_ODR_HIGH */
