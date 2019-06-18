/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(VBUS_WAKE,           PIN(B, 5),  GPIO_INT_BOTH,    vbus_event)
GPIO_INT(DPSRC_HPD,           PIN(B, 13), GPIO_INT_BOTH,    hpd_event)
GPIO_INT(DBG_12V_TO_DUT_L,    PIN(B, 14), GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_5V_TO_DUT_L,     PIN(B, 8),  GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_CHG_TO_DEV_L,    PIN(F, 1),  GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_USB_TOGGLE_L,    PIN(F, 0),  GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_CASE_CLOSE_EN_L, PIN(B, 12), GPIO_INT_FALLING, button_event)
GPIO_INT(DBG_MUX_FLIP_L,      PIN(B, 15), GPIO_INT_FALLING, button_event)

/* TODO(crosbug.com/p/33761) : This interrupt is double booked w/ HPD */
GPIO(DBG_20V_TO_DUT_L,    PIN(C, 13), GPIO_INPUT)

/* PD RX/TX */
GPIO(USBC_PD_REF,         PIN(A, 1),  GPIO_ANALOG)
GPIO(USBC_CC1_PD,         PIN(A, 0),  GPIO_ANALOG)
GPIO(USBC_CC1_TX_EN,      PIN(A, 3),  GPIO_OUT_LOW)
GPIO(USBC_CC2_PD,         PIN(A, 4),  GPIO_ANALOG)
GPIO(USBC_CC2_TX_EN,      PIN(B, 2),  GPIO_OUT_LOW)
GPIO(USBC_CC_TX_DATA,     PIN(A, 6),  GPIO_OUT_LOW)

#if 0
/* Alternate functions */
GPIO(USBC_TX_CLKOUT,      PIN(B, 9),  GPIO_OUT_LOW)
GPIO(USBC_TX_CLKIN,       PIN(A, 5),  GPIO_OUT_LOW)
#endif

/* USB-C Power and muxes control */
GPIO(USBC_CHARGE_EN,      PIN(A, 8),  GPIO_OUT_HIGH)
GPIO(USBC_CC1_DEVICE_ODL, PIN(A, 9),  GPIO_OUT_LOW)
GPIO(USBC_CC1_HOST,       PIN(A, 2),  GPIO_INPUT)
GPIO(USBC_CC2_DEVICE_ODL, PIN(B, 7),  GPIO_OUT_LOW)
GPIO(USBC_CC2_HOST,       PIN(B, 6),  GPIO_INPUT)
GPIO(USBC_POLARITY,       PIN(B, 1),  GPIO_OUT_HIGH)
GPIO(USBC_SS_USB_MODE,    PIN(B, 3),  GPIO_OUT_LOW)
GPIO(USB_CC1_VCONN_EN_L,  PIN(A, 11), GPIO_OUT_HIGH)
GPIO(USB_CC2_VCONN_EN_L,  PIN(A, 12), GPIO_OUT_HIGH)

GPIO(VBUS_CHARGER_EN,     PIN(B, 0),  GPIO_OUT_LOW)
/* VSEL_0/1: 0/0 = 5V, 1/0 = 12V, 1/1 = 20V */
GPIO(USBC_VSEL_1,         PIN(A, 10), GPIO_OUT_LOW)
GPIO(USBC_VSEL_0,         PIN(C, 14), GPIO_OUT_LOW)

/* Case closed debugging */
GPIO(CASE_CLOSE_EN,       PIN(A, 7),  GPIO_OUT_LOW)
GPIO(CASE_CLOSE_DFU_L,    PIN(A, 13), GPIO_OUT_HIGH)
GPIO(DEBUG_TOGGLE,        PIN(B, 4),  GPIO_OUT_LOW)

/* Alternate functions */
#if 0
GPIO(UART_TX,             PIN(A, 14), GPIO_OUT_LOW)
GPIO(UART_RX,             PIN(A, 15), GPIO_OUT_LOW)
#endif

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,       PIN(B, 10), GPIO_INPUT)
GPIO(MASTER_I2C_SDA,       PIN(B, 11), GPIO_INPUT)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x0020), 0, MODULE_USB_PD, 0) /* SPI1: SCK(PA5) */
ALTERNATE(PIN_MASK(B, 0x0200), 2, MODULE_USB_PD, 0) /* TIM17_CH1: (PB9) */
ALTERNATE(PIN_MASK(A, 0xC000), 1, MODULE_UART,   0) /* USART2: PA14/PA15 */
ALTERNATE(PIN_MASK(B, 0x0C00), 1, MODULE_I2C,    0) /* I2C MASTER:PB10/11 */
