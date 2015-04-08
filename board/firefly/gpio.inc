/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(VBUS_WAKE,         C, 13, GPIO_INT_BOTH, vbus_event)

/* Buttons */
GPIO_INT(SW_PP20000,        B, 10, GPIO_INT_FALLING, button_event)
GPIO_INT(SW_PP12000,        B, 11, GPIO_INT_FALLING, button_event)
GPIO_INT(SW_PP5000,         B, 12, GPIO_INT_FALLING, button_event)

/* PD RX/TX */
GPIO(USB_CC1_PD,        A, 0,  GPIO_ANALOG)
GPIO(PD_REF1,           A, 1,  GPIO_ANALOG)
GPIO(USB_CC2_PD,        A, 2,  GPIO_ANALOG)
GPIO(PD_REF2,           A, 3,  GPIO_ANALOG)
GPIO(PD_CC1_TX_EN,      A, 4,  GPIO_ODR_HIGH)
GPIO(PD_CC2_TX_EN,      A, 15, GPIO_ODR_HIGH)
GPIO(PD_CLK_OUT,        B, 9,  GPIO_OUT_LOW)
GPIO(PD_CC1_TX_DATA,    A, 6,  GPIO_INPUT)
GPIO(PD_CC2_TX_DATA,    B, 4,  GPIO_INPUT)
GPIO(PD_CLK_IN,         B, 3,  GPIO_INPUT)

/* CCx device pull-downs */
GPIO(PD_CC1_DEVICE,     B, 13, GPIO_ODR_LOW)
GPIO(PD_CC2_DEVICE,     B, 14, GPIO_ODR_LOW)

/* ADC */
GPIO(VBUS_SENSE,        A, 5,  GPIO_ANALOG)

/* LEDs control */
GPIO(LED_PP20000,       B, 0,  GPIO_OUT_LOW)
GPIO(LED_PP12000,       B, 1,  GPIO_OUT_LOW)
GPIO(LED_PP5000,        B, 2,  GPIO_OUT_LOW)

/* Slave I2C port */
GPIO(I2C_INT_L,         B, 8,  GPIO_ODR_HIGH)
/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C_SCL,           B, 6,  GPIO_INPUT)
GPIO(I2C_SDA,           B, 7,  GPIO_INPUT)

/* Test points */
GPIO(TP_A8,             A, 8,  GPIO_ODR_HIGH)
GPIO(TP_A13,            A, 13, GPIO_ODR_HIGH)
GPIO(TP_A14,            A, 14, GPIO_ODR_HIGH)
GPIO(TP_B15,            B, 15, GPIO_ODR_HIGH)
GPIO(TP_C14,            C, 14, GPIO_ODR_HIGH)
GPIO(TP_C15,            C, 15, GPIO_ODR_HIGH)
GPIO(TP_F0,             F, 0,  GPIO_ODR_HIGH)
GPIO(TP_F1,             F, 1,  GPIO_ODR_HIGH)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(B, 0x0008, 0, MODULE_USB_PD, 0)            /* SPI1: SCK(PB3) */
ALTERNATE(B, 0x0200, 2, MODULE_USB_PD, 0)            /* TIM17_CH1: PB9) */
ALTERNATE(A, 0x0600, 1, MODULE_UART,   GPIO_PULL_UP) /* USART1: PA9/PA10 */
ALTERNATE(B, 0x00c0, 1, MODULE_I2C,    0)            /* I2C SLAVE:PB6/7 */
