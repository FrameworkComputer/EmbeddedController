/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(VBUS_WAKE,           B, 5,  GPIO_INT_BOTH,    vbus_event)
GPIO_INT(DBG_20V_TO_DUT_L,    C, 13, GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_12V_TO_DUT_L,    B, 14, GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_5V_TO_DUT_L,     B, 8,  GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_CHG_TO_DEV_L,    F, 1,  GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_USB_TOGGLE_L,    F, 0,  GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_CASE_CLOSE_EN_L, B, 12, GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_MUX_FLIP_L,      B, 15, GPIO_INT_FALLING, button_event)

/* PD RX/TX */
GPIO(USBC_PD_REF,         A, 1,  GPIO_ANALOG)
GPIO(USBC_CC1_PD,         A, 0,  GPIO_ANALOG)
GPIO(USBC_CC1_TX_EN,      A, 3,  GPIO_OUT_LOW)
GPIO(USBC_CC2_PD,         A, 4,  GPIO_ANALOG)
GPIO(USBC_CC2_TX_EN,      B, 2,  GPIO_OUT_LOW)
GPIO(USBC_CC_TX_DATA,     A, 6,  GPIO_OUT_LOW)

#if 0
/* Alternate functions */
GPIO(USBC_TX_CLKOUT,      B, 9,  GPIO_OUT_LOW)
GPIO(USBC_TX_CLKIN,       A, 5,  GPIO_OUT_LOW)
#endif

/* USB-C Power and muxes control */
GPIO(USBC_CHARGE_EN,      A, 8,  GPIO_OUT_HIGH)
GPIO(USBC_CC1_DEVICE_ODL, A, 9,  GPIO_OUT_LOW)
GPIO(USBC_CC1_HOST,       A, 2,  GPIO_INPUT)
GPIO(USBC_CC2_DEVICE_ODL, B, 7,  GPIO_OUT_LOW)
GPIO(USBC_CC2_HOST,       B, 6,  GPIO_INPUT)
GPIO(USBC_POLARITY,       B, 1,  GPIO_OUT_HIGH)
GPIO(USBC_SS_USB_MODE,    B, 3,  GPIO_OUT_LOW)
GPIO(USB_CC1_VCONN_EN_L,  A, 11, GPIO_OUT_HIGH)
GPIO(USB_CC2_VCONN_EN_L,  A, 12, GPIO_OUT_HIGH)

GPIO(VBUS_CHARGER_EN,     B, 0,  GPIO_OUT_LOW)
/* VSEL_0/1: 0/0 = 5V, 1/0 = 12V, 1/1 = 20V */
GPIO(USBC_VSEL_1,         A, 10, GPIO_OUT_LOW)
GPIO(USBC_VSEL_0,         C, 14, GPIO_OUT_LOW)

/* Case closed debugging */
GPIO(CASE_CLOSE_EN,       A, 7,  GPIO_OUT_LOW)
GPIO(CASE_CLOSE_DFU_L,    A, 13, GPIO_OUT_HIGH)
GPIO(DEBUG_TOGGLE,        B, 4,  GPIO_OUT_LOW)

/* Display port */
GPIO(DPSRC_HPD,           B, 13, GPIO_INPUT)

/* Alternate functions */
#if 0
GPIO(UART_TX,             A, 14, GPIO_OUT_LOW)
GPIO(UART_RX,             A, 15, GPIO_OUT_LOW)
#endif

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,       B, 10, GPIO_INPUT)
GPIO(MASTER_I2C_SDA,       B, 11, GPIO_INPUT)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(A, 0x0020, 0, MODULE_USB_PD, 0) /* SPI1: SCK(PA5) */
ALTERNATE(B, 0x0200, 2, MODULE_USB_PD, 0) /* TIM17_CH1: (PB9) */
ALTERNATE(A, 0xC000, 1, MODULE_UART,   0) /* USART2: PA14/PA15 */
ALTERNATE(B, 0x0C00, 1, MODULE_I2C,    0) /* I2C MASTER:PB10/11 */
