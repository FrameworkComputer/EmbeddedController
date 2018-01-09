/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(FPS_INT,         PIN(A, 0), GPIO_INT_RISING, fps_event)
GPIO_INT(SPI1_NSS,        PIN(A, 4), GPIO_INPUT, spi_event)

/* Outputs */
GPIO(EC_INT_L,            PIN(A, 1),  GPIO_INPUT) /* Enabled when PCH is up */
GPIO(FP_RST_ODL,          PIN(E, 0),  GPIO_ODR_HIGH)
GPIO(SPI4_NSS,            PIN(E, 4),  GPIO_OUT_HIGH)

UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

/* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(A, 0x0600), GPIO_ALT_USART,  MODULE_UART, GPIO_PULL_UP)
/* SPI1 slave from the AP: PA4/5/6/7 */
ALTERNATE(PIN_MASK(A, 0x00f0), GPIO_ALT_SPI, MODULE_SPI,  0)
/* SPI4 master to sensor: PE2/5/6 */
ALTERNATE(PIN_MASK(E, 0x0064), GPIO_ALT_SPI, MODULE_SPI_MASTER,  0)
