/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(FPS_INT,         PIN(A, 0), GPIO_INT_RISING, fps_event)
GPIO_INT(SPI1_NSS,        PIN(A, 4), GPIO_INPUT, spi_event)

GPIO_INT(PCH_SLP_S0_L,    PIN(B, 0), GPIO_INT_BOTH, slp_event)
GPIO_INT(PCH_SLP_S3_L,    PIN(B, 1), GPIO_INT_BOTH, slp_event)
GPIO(PCH_SLP_S4_L,        PIN(B, 2), GPIO_INPUT)
GPIO(PCH_SLP_SUS_L,       PIN(B, 5), GPIO_INPUT)

GPIO(WP,                  PIN(B, 7), GPIO_INPUT)

/* Outputs */
GPIO(EC_INT_L,            PIN(A, 1),  GPIO_OUT_HIGH)
GPIO(FP_RST_ODL,          PIN(B,10),  GPIO_OUT_HIGH)
GPIO(SPI2_NSS,            PIN(B,12),  GPIO_OUT_HIGH)
GPIO(USER_PRES_L,         PIN(B, 9),  GPIO_ODR_HIGH)

/*
 * Unused pins.
 * Configuring unused pins as ANALOG INPUT to save power. For more info
 * look at "USING STM32F4 MCU POWER MODES WITH BEST DYNAMIC EFFICIENCY"
 * ("AN4365") section 1.2.6 and STM32F412 reference manual section 7.3.12.
 */
GPIO(UNUSED_PA2,            PIN(A, 2),  GPIO_ANALOG)
GPIO(UNUSED_PA3,            PIN(A, 3),  GPIO_ANALOG)
GPIO(UNUSED_PA8,            PIN(A, 8),  GPIO_ANALOG)
GPIO(UNUSED_PC13,           PIN(C, 13), GPIO_ANALOG)
GPIO(UNUSED_PC14,           PIN(C, 14), GPIO_ANALOG)
GPIO(UNUSED_PC15,           PIN(C, 15), GPIO_ANALOG)
GPIO(UNUSED_PH0,            PIN(H, 0),  GPIO_ANALOG)
GPIO(UNUSED_PH1,            PIN(H, 1),  GPIO_ANALOG)

UNIMPLEMENTED(ENTERING_RW)

/* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(A, 0x0600), GPIO_ALT_USART,  MODULE_UART, GPIO_PULL_UP)
/* SPI1 slave from the AP: PA4/5/6/7 */
ALTERNATE(PIN_MASK(A, 0x00f0), GPIO_ALT_SPI, MODULE_SPI,  0)
/*
 * SPI2 master to sensor: PB13/14/15
 * Note that we're not configuring NSS (PB12) here because we have already
 * configured it as a GPIO above and the SPI_MASTER module expects to use it
 * in software NSS management mode, not hardware management mode.
 */
ALTERNATE(PIN_MASK(B, 0xE000), GPIO_ALT_SPI, MODULE_SPI_MASTER,  0)
