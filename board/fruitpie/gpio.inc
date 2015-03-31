/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(VBUS_WAKE,         PIN(B, 5),  GPIO_INT_BOTH,    vbus_event)
GPIO_INT(MASTER_I2C_INT_L,  PIN(C, 13), GPIO_INT_FALLING, tsu_event)
GPIO_INT(ALERT_L,           PIN(A, 2),  GPIO_INT_FALLING, rohm_event)

/* PD RX/TX */
GPIO(USB_CC1_PD,        PIN(A, 0),  GPIO_ANALOG)
GPIO(PD_REF1,           PIN(A, 1),  GPIO_ANALOG)
GPIO(PD_REF2,           PIN(A, 3),  GPIO_ANALOG)
GPIO(USB_CC2_PD,        PIN(A, 4),  GPIO_ANALOG)
GPIO(PD_CLK_OUT,        PIN(B, 9),  GPIO_OUT_LOW)
GPIO(PD_TX_EN,          PIN(B, 12), GPIO_OUT_LOW)
GPIO(PD_TX_DATA,        PIN(B, 14), GPIO_OUT_LOW)
#if 0
GPIO(PD_CLK_IN,         PIN(B, 13), GPIO_OUT_LOW)
#endif

/* Power and muxes control */
GPIO(PP5000_EN,         PIN(A, 5),  GPIO_OUT_HIGH)
GPIO(CC_HOST,           PIN(A, 6),  GPIO_OUT_LOW)
GPIO(CHARGE_EN_L,       PIN(A, 8),  GPIO_OUT_LOW)
GPIO(USB_C_5V_EN,       PIN(A, 10), GPIO_OUT_LOW)
GPIO(VCONN1_EN,         PIN(B, 15), GPIO_OUT_LOW)
GPIO(VCONN2_EN,         PIN(C, 14), GPIO_OUT_LOW)
GPIO(SS1_EN_L,          PIN(A, 9),  GPIO_OUT_HIGH)
GPIO(SS2_EN_L,          PIN(B, 4),  GPIO_OUT_HIGH)
GPIO(SS2_USB_MODE_L,    PIN(B, 3),  GPIO_OUT_HIGH)
GPIO(SS1_USB_MODE_L,    PIN(B, 8),  GPIO_OUT_HIGH)
GPIO(DP_MODE,           PIN(C, 15), GPIO_OUT_LOW)
GPIO(DP_POLARITY_L,     PIN(A, 7),  GPIO_OUT_HIGH)

/* Not used : no host on that bus */
GPIO(SLAVE_I2C_INT_L,   PIN(B, 2),  GPIO_ODR_HIGH)

/* Alternate functions */
#if 0
GPIO(USB_DM,            PIN(A, 11), GPIO_ANALOG)
GPIO(USB_DP,            PIN(A, 12), GPIO_ANALOG)
GPIO(UART_TX,           PIN(A, 14), GPIO_OUT_LOW)
GPIO(UART_RX,           PIN(A, 15), GPIO_OUT_LOW)
#endif
/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(SLAVE_I2C_SCL,     PIN(B, 6),  GPIO_INPUT)
GPIO(SLAVE_I2C_SDA,     PIN(B, 7),  GPIO_INPUT)
GPIO(MASTER_I2C_SCL,    PIN(B, 10), GPIO_INPUT)
GPIO(MASTER_I2C_SDA,    PIN(B, 11), GPIO_INPUT)

/* Rohm BD92104 connections */
GPIO(USBPD_RST,         PIN(B, 0),  GPIO_OUT_LOW)
GPIO(USBPD_FORCE_OTG,   PIN(B, 1),  GPIO_OUT_LOW)
GPIO(USBPD_VIN_EN_L,    PIN(F, 0),  GPIO_OUT_HIGH)

/* Test points */
GPIO(TP9,               PIN(A, 13), GPIO_ODR_HIGH)
GPIO(TP11,              PIN(F, 1),  GPIO_ODR_HIGH)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(B, 0x2000), 0, MODULE_USB_PD, 0) /* SPI2: SCK(PB13) */
ALTERNATE(PIN_MASK(B, 0x0200), 2, MODULE_USB_PD, 0) /* TIM17_CH1: PB9) */
ALTERNATE(PIN_MASK(A, 0xC000), 1, MODULE_UART,   0) /* USART2: PA14/PA15 */
ALTERNATE(PIN_MASK(B, 0x0cc0), 1, MODULE_I2C,    0) /* I2C SLAVE:PB6/7 MASTER:PB10/11 */
