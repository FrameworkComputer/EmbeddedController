/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(CC2_ALERT_L,  PIN(A, 7),  GPIO_INT_BOTH | GPIO_PULL_UP, cc2_event)
GPIO_INT(VBUS_ALERT_L, PIN(B, 2),  GPIO_INT_BOTH | GPIO_PULL_UP, vbus_event)

GPIO(CC1_EN,       PIN(A, 0),  GPIO_OUT_HIGH)
GPIO(CC1_PD,       PIN(A, 1),  GPIO_ANALOG)
GPIO(CC2_EN,       PIN(A, 2),  GPIO_OUT_HIGH)
GPIO(CC2_PD,       PIN(A, 3),  GPIO_ANALOG)
GPIO(DAC,          PIN(A, 4),  GPIO_ANALOG)
GPIO(CC2_TX_DATA,  PIN(A, 6),  GPIO_OUT_LOW)

GPIO(CC1_RA,       PIN(A, 8),  GPIO_ODR_HIGH)
GPIO(USB_DM,       PIN(A, 11), GPIO_ANALOG)
GPIO(USB_DP,       PIN(A, 12), GPIO_ANALOG)
GPIO(CC1_RPUSB,    PIN(A, 13), GPIO_ODR_HIGH)
GPIO(CC1_RP1A5,    PIN(A, 14), GPIO_ODR_HIGH)
GPIO(CC1_RP3A0,    PIN(A, 15), GPIO_ODR_HIGH)
GPIO(CC2_RPUSB,    PIN(B, 0),  GPIO_ODR_HIGH)

GPIO(CC1_TX_EN,    PIN(B, 1),  GPIO_OUT_LOW)
GPIO(CC2_TX_EN,    PIN(B, 3),  GPIO_OUT_LOW)
GPIO(CC1_TX_DATA,  PIN(B, 4),  GPIO_OUT_LOW)
GPIO(CC1_RD,       PIN(B, 5),  GPIO_ODR_HIGH)
GPIO(I2C_SCL,      PIN(B, 6),  GPIO_INPUT)
GPIO(I2C_SDA,      PIN(B, 7),  GPIO_INPUT)
GPIO(CC2_RD,       PIN(B, 8),  GPIO_ODR_HIGH)
GPIO(LED_G_L,      PIN(B, 11), GPIO_ODR_HIGH)
GPIO(LED_R_L,      PIN(B, 13), GPIO_ODR_HIGH)
GPIO(LED_B_L,      PIN(B, 14), GPIO_ODR_HIGH)
GPIO(CC2_RA,       PIN(B, 15), GPIO_ODR_HIGH)
GPIO(CC2_RP1A5,    PIN(C, 14), GPIO_ODR_HIGH)
GPIO(CC2_RP3A0,    PIN(C, 15), GPIO_ODR_HIGH)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x0020), 0, MODULE_USB_PD, 0)            /* SPI1: SCK(PA5) */
ALTERNATE(PIN_MASK(B, 0x0200), 2, MODULE_USB_PD, 0)            /* TIM17_CH1: PB9 */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   GPIO_PULL_UP) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x00C0), 1, MODULE_I2C,    0)            /* I2C1 MASTER:PB6/7 */
