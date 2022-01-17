/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO(WP_L,			PIN(A, 13), GPIO_INPUT | GPIO_PULL_UP)

#ifdef SECTION_IS_RW

GPIO(SDB_L,		PIN(A, 0), GPIO_OUT_LOW)
GPIO(SPI1_CS1_L,	PIN(A, 1), GPIO_OUT_HIGH)
GPIO(SPI1_CS2_L,	PIN(A, 2), GPIO_OUT_HIGH)
GPIO(L_POWER,		PIN(A, 3), GPIO_OUT_LOW)

/* SPI Controller: PA5, 6, 7 */
ALTERNATE(PIN_MASK(A, 0x00e0), 0, MODULE_SPI_CONTROLLER, GPIO_PULL_UP)


#endif /* SECTION_IS_RW */

/* USART1: PA09/PA10 - Servo stm32 console UART */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART, GPIO_PULL_UP)

UNIMPLEMENTED(ENTERING_RW)
