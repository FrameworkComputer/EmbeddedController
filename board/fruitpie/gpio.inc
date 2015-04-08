/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(VBUS_WAKE,         B, 5,  GPIO_INT_BOTH,    vbus_event)
GPIO_INT(MASTER_I2C_INT_L,  C, 13, GPIO_INT_FALLING, tsu_event)
GPIO_INT(ALERT_L,           A, 2,  GPIO_INT_FALLING, rohm_event)

/* PD RX/TX */
GPIO(USB_CC1_PD,        A, 0,  GPIO_ANALOG)
GPIO(PD_REF1,           A, 1,  GPIO_ANALOG)
GPIO(PD_REF2,           A, 3,  GPIO_ANALOG)
GPIO(USB_CC2_PD,        A, 4,  GPIO_ANALOG)
GPIO(PD_CLK_OUT,        B, 9,  GPIO_OUT_LOW)
GPIO(PD_TX_EN,          B, 12, GPIO_OUT_LOW)
GPIO(PD_TX_DATA,        B, 14, GPIO_OUT_LOW)
#if 0
GPIO(PD_CLK_IN,         B, 13, GPIO_OUT_LOW)
#endif

/* Power and muxes control */
GPIO(PP5000_EN,         A, 5,  GPIO_OUT_HIGH)
GPIO(CC_HOST,           A, 6,  GPIO_OUT_LOW)
GPIO(CHARGE_EN_L,       A, 8,  GPIO_OUT_LOW)
GPIO(USB_C_5V_EN,       A, 10, GPIO_OUT_LOW)
GPIO(VCONN1_EN,         B, 15, GPIO_OUT_LOW)
GPIO(VCONN2_EN,         C, 14, GPIO_OUT_LOW)
GPIO(SS1_EN_L,          A, 9,  GPIO_OUT_HIGH)
GPIO(SS2_EN_L,          B, 4,  GPIO_OUT_HIGH)
GPIO(SS2_USB_MODE_L,    B, 3,  GPIO_OUT_HIGH)
GPIO(SS1_USB_MODE_L,    B, 8,  GPIO_OUT_HIGH)
GPIO(DP_MODE,           C, 15, GPIO_OUT_LOW)
GPIO(DP_POLARITY_L,     A, 7,  GPIO_OUT_HIGH)

/* Not used : no host on that bus */
GPIO(SLAVE_I2C_INT_L,   B, 2,  GPIO_ODR_HIGH)

/* Alternate functions */
#if 0
GPIO(USB_DM,            A, 11, GPIO_ANALOG)
GPIO(USB_DP,            A, 12, GPIO_ANALOG)
GPIO(UART_TX,           A, 14, GPIO_OUT_LOW)
GPIO(UART_RX,           A, 15, GPIO_OUT_LOW)
#endif
/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(SLAVE_I2C_SCL,     B, 6,  GPIO_INPUT)
GPIO(SLAVE_I2C_SDA,     B, 7,  GPIO_INPUT)
GPIO(MASTER_I2C_SCL,    B, 10, GPIO_INPUT)
GPIO(MASTER_I2C_SDA,    B, 11, GPIO_INPUT)

/* Rohm BD92104 connections */
GPIO(USBPD_RST,         B, 0,  GPIO_OUT_LOW)
GPIO(USBPD_FORCE_OTG,   B, 1,  GPIO_OUT_LOW)
GPIO(USBPD_VIN_EN_L,    F, 0,  GPIO_OUT_HIGH)

/* Test points */
GPIO(TP9,               A, 13, GPIO_ODR_HIGH)
GPIO(TP11,              F, 1,  GPIO_ODR_HIGH)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(B, 0x2000, 0, MODULE_USB_PD, 0) /* SPI2: SCK(PB13) */
ALTERNATE(B, 0x0200, 2, MODULE_USB_PD, 0) /* TIM17_CH1: PB9) */
ALTERNATE(A, 0xC000, 1, MODULE_UART,   0) /* USART2: PA14/PA15 */
ALTERNATE(B, 0x0cc0, 1, MODULE_I2C,    0) /* I2C SLAVE:PB6/7 MASTER:PB10/11 */
