/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(FPS_INT,         PIN(A, 0), GPIO_INT_RISING, fps_event)
GPIO_INT(SPI1_NSS,        PIN(A, 4), GPIO_INPUT, spi_event)

GPIO_INT(PCH_SLP_S0_L,    PIN(D,13), GPIO_INT_BOTH, slp_event)
GPIO_INT(PCH_SLP_S3_L,    PIN(A,11), GPIO_INT_BOTH, slp_event)

GPIO(PCH_SLP_S4_L,        PIN(D, 8), GPIO_INPUT)
GPIO(PCH_SLP_SUS_L,       PIN(D, 3), GPIO_INPUT)
#if defined(APPLY_RESET_LOOP_FIX) && defined(SECTION_IS_RW)
GPIO_INT(WP,              PIN(B, 7), GPIO_INT_BOTH, wp_event)
#else
GPIO(WP,                  PIN(B, 7), GPIO_INPUT)
#endif

/* Outputs */
GPIO(EC_INT_L,            PIN(A, 1),  GPIO_OUT_HIGH)
GPIO(FP_RST_ODL,          PIN(E, 0),  GPIO_OUT_HIGH)
GPIO(SPI4_NSS,            PIN(E, 4),  GPIO_OUT_HIGH)
GPIO(USER_PRES_L,         PIN(C, 5),  GPIO_ODR_HIGH)

UNIMPLEMENTED(ENTERING_RW)

/* USART1: PA9/PA10 (TX/RX) */
ALTERNATE(PIN_MASK(A, 0x0600), GPIO_ALT_USART,  MODULE_UART, GPIO_PULL_UP)
/* SPI1 slave from the AP: PA4/5/6/7 (CS/CLK/MISO/MOSI) */
ALTERNATE(PIN_MASK(A, 0x00f0), GPIO_ALT_SPI, MODULE_SPI,  0)
/* SPI4 master to sensor: PE2/5/6 (CLK/MISO/MOSI) */
ALTERNATE(PIN_MASK(E, 0x0064), GPIO_ALT_SPI, MODULE_SPI_MASTER,  0)
