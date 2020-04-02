/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(FPS_INT,         PIN(A, 0), GPIO_INT_RISING, fps_event)
GPIO_INT(PCH_SLP_S0_L,    PIN(A, 8), GPIO_INT_BOTH, slp_event)
GPIO_INT(SPI1_NSS,        PIN(A, 4), GPIO_INPUT, spi_event)
GPIO_INT(PCH_SLP_S3_L,    PIN(B, 6), GPIO_INT_BOTH, slp_event)

/* Inputs */
GPIO(FP_SENSOR_SEL,       PIN(B, 0), GPIO_INPUT)
GPIO(TRANSPORT_SEL,       PIN(B, 1), GPIO_INPUT)
GPIO(WP,                  PIN(B, 7), GPIO_INPUT)

/* Outputs */
GPIO(EC_INT_L,            PIN(A, 1),  GPIO_OUT_HIGH)
GPIO(FP_RST_ODL,          PIN(B,10),  GPIO_OUT_HIGH)
GPIO(SPI2_NSS,            PIN(B,12),  GPIO_OUT_HIGH)
GPIO(DIVIDER_HIGHSIDE,    PIN(B, 8),  GPIO_OUT_LOW)
GPIO(USER_PRES_L,         PIN(B, 9),  GPIO_ODR_HIGH)

/*
 * Unused pins.
 * Configuring unused pins as ANALOG INPUT to save power. For more info
 * look at "USING STM32F4 MCU POWER MODES WITH BEST DYNAMIC EFFICIENCY"
 * ("AN4365") section 1.2.6 and STM32F412 reference manual section 7.3.12.
 */
UNUSED(PIN(B, 2))
UNUSED(PIN(B, 5))
UNUSED(PIN(C, 13))
UNUSED(PIN(C, 14))
UNUSED(PIN(C, 15))
UNUSED(PIN(H, 0))
UNUSED(PIN(H, 1))

UNIMPLEMENTED(ENTERING_RW)

/* USART1: PA9/PA10 (TX/RX) to AP*/
ALTERNATE(PIN_MASK(A, 0x0600), GPIO_ALT_USART,  MODULE_UART, GPIO_PULL_UP)
/* USART2: PA2/PA3 (TX/RX) to servo*/
ALTERNATE(PIN_MASK(A, 0x000C), GPIO_ALT_USART,  MODULE_UART, GPIO_PULL_UP)
/* SPI1 slave from the AP: PA4/5/6/7 (CS/CLK/MISO/MOSI) */
ALTERNATE(PIN_MASK(A, 0x00f0), GPIO_ALT_SPI, MODULE_SPI,  0)
/*
 * SPI2 master to sensor: PB13/14/15 (CLK/MISO/MOSI)
 * Note that we're not configuring NSS (PB12) here because we have already
 * configured it as a GPIO above and the SPI_MASTER module expects to use it
 * in software NSS management mode, not hardware management mode.
 */
ALTERNATE(PIN_MASK(B, 0xE000), GPIO_ALT_SPI, MODULE_SPI_MASTER, GPIO_PULL_DOWN)
