/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Write protect */

GPIO(EC_FLASH_WP_ODL,               PIN(A,  3), GPIO_ODR_HIGH)
GPIO(EC_WP_L,                       PIN(E, 11), GPIO_INT_BOTH)

/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)

/* USART3_TX/RX GPIOC 10-11*/
ALTERNATE(PIN_MASK(C, 0x0C00),  7, MODULE_UART,  GPIO_PULL_UP)
