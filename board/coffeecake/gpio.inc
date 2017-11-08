/* -*- mode:c -*-
 *
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(DP_HPD,           PIN(A, 0),  GPIO_INT_BOTH, hpd_event)
GPIO_INT(CHARGER_INT,      PIN(C, 13), GPIO_INT_FALLING, sy21612_int)
GPIO_INT(USB_C_VBUS_DET_L, PIN(C, 14), GPIO_INT_BOTH, vbus_event)

GPIO(USB_C_CC1_PD,     PIN(A, 1),  GPIO_ANALOG)
GPIO(VBUS_DIV4_MON,    PIN(A, 2),  GPIO_ANALOG)
GPIO(MCDP_RESET_L,     PIN(A, 3),  GPIO_OUT_HIGH)
GPIO(PD_DAC_REF_TP28,  PIN(A, 4),  GPIO_ANALOG)
GPIO(DAC_VBUS_VOLT,    PIN(A, 5),  GPIO_ANALOG)
GPIO(LED_GREEN,        PIN(A, 6),  GPIO_OUT_LOW)
GPIO(LED_BLUE,         PIN(A, 7),  GPIO_OUT_LOW)

GPIO(PD_SBU_ENABLE,    PIN(A, 8),  GPIO_OUT_LOW)
GPIO(USB_DM,           PIN(A, 11), GPIO_ANALOG)
GPIO(USB_DP,           PIN(A, 12), GPIO_ANALOG)
GPIO(PD_DISCHARGE,     PIN(A, 13), GPIO_OUT_LOW)
GPIO(PD_CC1_ODL,       PIN(A, 15), GPIO_OUT_LOW)

GPIO(EN_PP3300,        PIN(B, 0),  GPIO_OUT_HIGH)
GPIO(MCU_PB1,          PIN(B, 1),  GPIO_OUT_LOW)
GPIO(PD_MCDP_SPI_WP_L, PIN(B, 2),  GPIO_OUT_LOW)
GPIO(PD_CC1_TX_DATA,   PIN(B, 4),  GPIO_INPUT)
GPIO(PD_CC1_HOST_HIGH, PIN(B, 5),  GPIO_INPUT)
GPIO(I2C0_SCL,         PIN(B, 6),  GPIO_INPUT)
GPIO(I2C0_SDA,         PIN(B, 7),  GPIO_INPUT)
GPIO(LED_ORANGE,       PIN(B, 9),  GPIO_OUT_LOW)
GPIO(PD_MCDP_SPI_CS_L, PIN(B, 12), GPIO_INPUT)

GPIO(AC_PRESENT_L,     PIN(C, 15), GPIO_INPUT)

GPIO(EN_PP5000,        PIN(F,  0), GPIO_OUT_HIGH)
GPIO(EN_USB_PD,        PIN(F,  1), GPIO_OUT_HIGH)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(B, 0x0008), 0, MODULE_USB_PD, 0)            /* SPI1: SCK(PB3) */
ALTERNATE(PIN_MASK(B, 0x0100), 2, MODULE_USB_PD, 0)            /* TIM16_CH1: PB9 */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   GPIO_PULL_UP) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x0C00), 4, MODULE_UART,   GPIO_PULL_UP) /* USART3: PB10/PB11 */
ALTERNATE(PIN_MASK(B, 0x00C0), 1, MODULE_I2C,   0)             /* I2C MASTER:PB6/7 */
