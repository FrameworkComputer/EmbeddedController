/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(USB_C0_VBUS_WAKE_L,     PIN(C, 14),  GPIO_INT_BOTH,    pd_vbus_evt_p0)

/* PD RX/TX */
GPIO(USB_C0_CC1_PD,          PIN(A, 2),  GPIO_ANALOG)
GPIO(USB_C_REF,              PIN(A, 1),  GPIO_ANALOG)
GPIO(USB_C0_CC2_PD,          PIN(A, 4),  GPIO_ANALOG)

GPIO(USB_C0_CC1_TX_DATA,     PIN(B, 4),  GPIO_INPUT)
GPIO(USB_C0_CC2_TX_DATA,     PIN(A, 6),  GPIO_INPUT)
GPIO(USB_PD_VBUS_WAKE,       PIN(C, 13), GPIO_INPUT)

GPIO(PP3300_USB_PD_EN,       PIN(A, 15), GPIO_OUT_HIGH)
GPIO(USB_C0_CC1_VCONN1_EN,   PIN(B, 1),  GPIO_OUT_LOW)
GPIO(USB_C0_CC2_VCONN1_EN,   PIN(B, 2),  GPIO_OUT_LOW)

GPIO(USB_C0_HOST_HIGH,       PIN(A, 3),  GPIO_OUT_LOW)
GPIO(USB_C0_CC1_ODL,         PIN(A, 11), GPIO_ODR_LOW)
GPIO(USB_C0_CC2_ODL,         PIN(A, 12), GPIO_ODR_LOW)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(SLAVE_I2C_SCL,          PIN(B, 6),  GPIO_INPUT)
GPIO(SLAVE_I2C_SDA,          PIN(B, 7),  GPIO_INPUT)

GPIO(EC_INT,                 PIN(A, 14), GPIO_ODR_HIGH)

UNIMPLEMENTED(WP_L)
UNIMPLEMENTED(ENTERING_RW)

#if 0
/* Alternate functions */
GPIO(USB_C0_TX_CLKOUT,       PIN(B, 8),  GPIO_OUT_LOW)
GPIO(USB_C0_TX_CLKIN,        PIN(B, 3),  GPIO_OUT_LOW)
#endif

ALTERNATE(PIN_MASK(B, 0x0008), 0, MODULE_USB_PD, 0) /* SPI1: SCK(PB3) */
ALTERNATE(PIN_MASK(B, 0x0100), 2, MODULE_USB_PD, 0) /* TIM16_CH1: PB8   */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   0) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,    0) /* I2C SLAVE:PB6/7  */
