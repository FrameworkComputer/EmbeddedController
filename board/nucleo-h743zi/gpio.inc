/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Note that these pins map to the Nucleo-H743ZI V2 and are only slightly
 * compatible with the original version.
 *
 * The V2 is denoted by "Nucleo-H743ZI2" vs. "Nucleo-H743ZI".
 */

/* Interrupts */
GPIO_INT(SPI1_NSS,        PIN(A, 4), GPIO_INPUT, spi_event)

GPIO_INT(PCH_SLP_S0_L,    PIN(D,13), GPIO_INT_BOTH, slp_event)
GPIO_INT(PCH_SLP_S3_L,    PIN(A,11), GPIO_INT_BOTH, slp_event)

GPIO(WP,                  PIN(B, 7), GPIO_INPUT)

/* Outputs */
GPIO(EC_INT_L,            PIN(A, 1),  GPIO_OUT_HIGH)
GPIO(LED1,                PIN(B, 0),  GPIO_OUT_LOW) /* Green */
GPIO(LED2,                PIN(E, 1),  GPIO_OUT_LOW) /* Yellow */
GPIO(LED3,                PIN(B, 14), GPIO_OUT_LOW) /* Red */

UNIMPLEMENTED(ENTERING_RW)

/* USART3: PD8/PD9 */
ALTERNATE(PIN_MASK(D, 0x0300), GPIO_ALT_USART,  MODULE_UART, GPIO_PULL_UP)
/* SPI1 slave from the AP: PA4/5/6/7 */
ALTERNATE(PIN_MASK(A, 0x00f0), GPIO_ALT_SPI, MODULE_SPI,  0)
