/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(VBUS_WAKE,         PIN(C, 13), GPIO_INT_BOTH, vbus_event)

/* Buttons */
GPIO_INT(SW_PP20000,        PIN(B, 10), GPIO_INT_FALLING, button_event)
GPIO_INT(SW_PP12000,        PIN(B, 11), GPIO_INT_FALLING, button_event)
GPIO_INT(SW_PP5000,         PIN(B, 12), GPIO_INT_FALLING, button_event)

/* PD RX/TX */
GPIO(USB_CC1_PD,        PIN(A, 0),  GPIO_ANALOG)
GPIO(PD_REF1,           PIN(A, 1),  GPIO_ANALOG)
GPIO(USB_CC2_PD,        PIN(A, 2),  GPIO_ANALOG)
GPIO(PD_REF2,           PIN(A, 3),  GPIO_ANALOG)
GPIO(PD_CC1_TX_EN,      PIN(A, 4),  GPIO_ODR_HIGH)
GPIO(PD_CC2_TX_EN,      PIN(A, 15), GPIO_ODR_HIGH)
GPIO(PD_CLK_OUT,        PIN(B, 9),  GPIO_OUT_LOW)
GPIO(PD_CC1_TX_DATA,    PIN(A, 6),  GPIO_INPUT)
GPIO(PD_CC2_TX_DATA,    PIN(B, 4),  GPIO_INPUT)
GPIO(PD_CLK_IN,         PIN(B, 3),  GPIO_INPUT)

/* CCx device pull-downs */
GPIO(PD_CC1_DEVICE,     PIN(B, 13), GPIO_ODR_LOW)
GPIO(PD_CC2_DEVICE,     PIN(B, 14), GPIO_ODR_LOW)

/* ADC */
GPIO(VBUS_SENSE,        PIN(A, 5),  GPIO_ANALOG)

/* LEDs control */
GPIO(LED_PP20000,       PIN(B, 0),  GPIO_OUT_LOW)
GPIO(LED_PP12000,       PIN(B, 1),  GPIO_OUT_LOW)
GPIO(LED_PP5000,        PIN(B, 2),  GPIO_OUT_LOW)

/* Slave I2C port */
GPIO(I2C_INT_L,         PIN(B, 8),  GPIO_ODR_HIGH)
/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C_SCL,           PIN(B, 6),  GPIO_INPUT)
GPIO(I2C_SDA,           PIN(B, 7),  GPIO_INPUT)

/* Test points */
GPIO(TP_A8,             PIN(A, 8),  GPIO_ODR_HIGH)
GPIO(TP_A13,            PIN(A, 13), GPIO_ODR_HIGH)
GPIO(TP_A14,            PIN(A, 14), GPIO_ODR_HIGH)
GPIO(TP_B15,            PIN(B, 15), GPIO_ODR_HIGH)
GPIO(TP_C14,            PIN(C, 14), GPIO_ODR_HIGH)
GPIO(TP_C15,            PIN(C, 15), GPIO_ODR_HIGH)
GPIO(TP_F0,             PIN(F, 0),  GPIO_ODR_HIGH)
GPIO(TP_F1,             PIN(F, 1),  GPIO_ODR_HIGH)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(B, 0x0008), 0, MODULE_USB_PD, 0)            /* SPI1: SCK(PB3) */
ALTERNATE(PIN_MASK(B, 0x0200), 2, MODULE_USB_PD, 0)            /* TIM17_CH1: PB9) */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   GPIO_PULL_UP) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,    0)            /* I2C SLAVE:PB6/7 */
