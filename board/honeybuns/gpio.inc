/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(DP_HPD,        PIN(A, 0),  GPIO_INT_BOTH, hpd_event)

/* PD RX/TX */
GPIO(USB_CC1_PD,        PIN(A, 1),  GPIO_ANALOG)
GPIO(PD_VBUS_P,         PIN(A, 5),  GPIO_ANALOG)
GPIO(PD_VBUS_N,         PIN(A, 6),  GPIO_ANALOG)
GPIO(PD_TX_EN,          PIN(A, 15), GPIO_OUT_LOW)
GPIO(PD_TX_DATA,        PIN(B, 4),  GPIO_OUT_LOW)

/* Power and muxes control */
GPIO(PP20000_EN,        PIN(A, 8),  GPIO_OUT_LOW)
GPIO(PP12000_EN,        PIN(A, 14), GPIO_OUT_LOW)
GPIO(PPVAR_VBUS_EN,     PIN(B, 12), GPIO_OUT_LOW)
GPIO(SS_MUX_OE_L,       PIN(B, 13), GPIO_OUT_HIGH)
GPIO(SS_MUX_SEL,        PIN(B, 14), GPIO_OUT_LOW)

/* Display Port/HDMI */
GPIO(PD_SBU_ENABLE,     PIN(A, 7),  GPIO_OUT_LOW)

/* Chip Resets */
GPIO(BRIDGE_RESET_L,    PIN(B, 0),  GPIO_OUT_LOW)
GPIO(SPLITTER_RESET_L,  PIN(B, 1),  GPIO_OUT_LOW)
GPIO(HUB_RESET_L,       PIN(B, 15), GPIO_OUT_LOW)


/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,    PIN(B, 6),  GPIO_INPUT)
GPIO(MASTER_I2C_SDA,    PIN(B, 7),  GPIO_INPUT)


/* Test points */
GPIO(TP6,               PIN(A, 13), GPIO_OUT_HIGH /*GPIO_ODR_HIGH*/)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(B, 0x0018), 0, MODULE_USB_PD, 0)            /* SPI1 SCK/MISO: PB3/PB4 */
ALTERNATE(PIN_MASK(B, 0x0200), 2, MODULE_USB_PD, 0)            /* TIM17_CH1: PB9 */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   GPIO_PULL_UP) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(A, 0x000C), 1, MODULE_UART,   GPIO_PULL_UP) /* USART2: PA2/PA3 */
ALTERNATE(PIN_MASK(B, 0x0C00), 4, MODULE_UART,   GPIO_PULL_UP) /* USART3: PB10/PB11 */
ALTERNATE(PIN_MASK(B, 0x00C0), 1, MODULE_I2C,    0)            /* I2C MASTER: PB6/PB7 */
