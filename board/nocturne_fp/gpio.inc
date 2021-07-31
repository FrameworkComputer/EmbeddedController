/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(SLP_L,           PIN(D,13), GPIO_INT_BOTH, slp_event)
GPIO_INT(SLP_ALT_L,       PIN(A,11), GPIO_INT_BOTH, slp_event)
GPIO_INT(SPI1_NSS,        PIN(A, 4), GPIO_INPUT, spi_event)

/* Inputs */
#ifndef APPLY_RESET_LOOP_FIX
GPIO(WP,                  PIN(B, 7), GPIO_INPUT)
#endif

/* Outputs */
GPIO(EC_INT_L,            PIN(A, 1),  GPIO_OUT_HIGH)

UNIMPLEMENTED(ENTERING_RW)

/* USART1: PA9/PA10 (TX/RX) */
ALTERNATE(PIN_MASK(A, 0x0600), GPIO_ALT_USART,  MODULE_UART, GPIO_PULL_UP)
/* SPI1 slave from the AP: PA4/5/6/7 (CS/CLK/MISO/MOSI) */
ALTERNATE(PIN_MASK(A, 0x00f0), GPIO_ALT_SPI, MODULE_SPI,  0)

#ifdef SECTION_IS_RW
#include "gpio_rw.inc"
#endif
