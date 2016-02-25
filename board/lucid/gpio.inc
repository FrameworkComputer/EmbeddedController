/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Interrupts */
GPIO_INT(AC_PRESENT,         PIN(C, 13), GPIO_INT_BOTH, vbus_evt)       /* AC power present */
GPIO_INT(CHARGE_STATUS,      PIN(F, 1),  GPIO_INT_BOTH, charge_state_interrupt)

/* PD RX/TX */
GPIO(USB_C0_CC1_PD,          PIN(A, 1),  GPIO_ANALOG)
GPIO(USB_C0_CC2_PD,          PIN(A, 3),  GPIO_ANALOG)
GPIO(USB_C0_CC1_TX_DATA,     PIN(B, 4),  GPIO_INPUT)
GPIO(USB_C0_CC2_TX_DATA,     PIN(A, 6),  GPIO_INPUT)

#if 0
/* Alternate functions */
GPIO(USB_C0_TX_CLKOUT,       PIN(B, 15), GPIO_OUT_LOW)
GPIO(USB_C0_TX_CLKIN,        PIN(B, 3),  GPIO_OUT_LOW)
#endif

/* Other inputs */
GPIO(PPVAR_VBUS_SENSE,       PIN(A, 7),  GPIO_ANALOG)
GPIO(BAT_PRESENT,            PIN(A, 14), GPIO_INPUT)
GPIO(USB_C0_N,               PIN(A, 11), GPIO_INPUT)
GPIO(USB_C0_P,               PIN(A, 12), GPIO_INPUT)

/* Other outputs */
GPIO(AP_INT_L,               PIN(A, 13), GPIO_ODR_HIGH)
GPIO(USB_C_CC_EN,            PIN(A, 15), GPIO_OUT_HIGH)
GPIO(BAT_LED_GREEN,          PIN(C, 14), GPIO_ODR_HIGH)
GPIO(BAT_LED_RED,            PIN(C, 15), GPIO_ODR_HIGH)
GPIO(BAT_LED_BLUE,           PIN(F, 0),  GPIO_ODR_HIGH)

UNIMPLEMENTED(ENTERING_RW)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(SLAVE_I2C_SCL,          PIN(B, 6),  GPIO_INPUT)
GPIO(SLAVE_I2C_SDA,          PIN(B, 7),  GPIO_INPUT)
GPIO(MASTER_I2C_SCL,         PIN(B, 10), GPIO_INPUT)
GPIO(MASTER_I2C_SDA,         PIN(B, 11), GPIO_INPUT)

ALTERNATE(PIN_MASK(B, 0x0008), 0, MODULE_USB_PD, 0) /* SPI1: SCK(PB3) */
ALTERNATE(PIN_MASK(B, 0x8000), 1, MODULE_USB_PD, 0) /* TIM15_CH2: PB15) */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   0) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x0cc0), 1, MODULE_I2C,    0) /* I2C SLAVE:PB6/7 MASTER:PB10/11 */
