/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(KB_IN00,        PIN(B, 8),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN01,        PIN(B, 9),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN02,        PIN(B, 10), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN03,        PIN(B, 11), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN04,        PIN(B, 12), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN05,        PIN(B, 13), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN06,        PIN(B, 14), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN07,        PIN(B, 15), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

/* TODO(crosbug.com/p/23494): interrupt handler for power button */
GPIO(KBD_PWR_BUTTON, PIN(B, 2),  GPIO_INPUT)

GPIO(OMZO_RDY_L,     PIN(A, 0),  GPIO_INPUT) /* PA0_WKUP */
GPIO(OZMO_RST_L,     PIN(A, 2),  GPIO_ODR_HIGH)
GPIO(VBUS_UP_DET,    PIN(A, 3),  GPIO_INPUT)
GPIO(OZMO_REQ_L,     PIN(A, 8),  GPIO_INPUT)
GPIO(CHARGE_ZERO,    PIN(B, 0),  GPIO_INPUT)
GPIO(CHARGE_SHUNT,   PIN(B, 1),  GPIO_INPUT)
GPIO(PMIC_INT_L,     PIN(B, 5),  GPIO_INPUT)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C1_SCL,       PIN(B, 6),  GPIO_INPUT)
GPIO(I2C1_SDA,       PIN(B, 7),  GPIO_INPUT)

GPIO(KB_OUT00,       PIN(C, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,       PIN(C, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT02,       PIN(C, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT03,       PIN(C, 3),  GPIO_KB_OUTPUT)
GPIO(KB_OUT04,       PIN(C, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT05,       PIN(C, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT06,       PIN(C, 6),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07,       PIN(C, 7),  GPIO_KB_OUTPUT)
GPIO(KB_OUT08,       PIN(C, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,       PIN(C, 9),  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,       PIN(C, 10), GPIO_KB_OUTPUT)
GPIO(KB_OUT11,       PIN(C, 11), GPIO_KB_OUTPUT)
GPIO(KB_OUT12,       PIN(C, 12), GPIO_KB_OUTPUT)
GPIO(USB_VBUS_CTRL,  PIN(C, 13), GPIO_OUT_LOW)
GPIO(HUB_RESET,      PIN(C, 14), GPIO_ODR_HIGH)
GPIO(WP_L,           PIN(D, 2),  GPIO_INPUT)

/*
 * TODO(crosbug.com/p/23494): This will be an alternate function GPIO,
 * so remove it from here.
 */
GPIO(BL_PWM,         PIN(A, 1),  GPIO_OUTPUT)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(EC_INT)
UNIMPLEMENTED(ENTERING_RW)

#if 0
/* Other GPIOs (probably need to be set up below as alt. function) */
GPIO(STM_USBDM,      PIN(A, 11), GPIO_DEFAULT)
GPIO(STM_USBDP,      PIN(A, 12), GPIO_DEFAULT)
GPIO(JTMS_SWDIO,     PIN(A, 13), GPIO_DEFAULT)
GPIO(JTCK_SWCLK,     PIN(A, 14), GPIO_DEFAULT)
GPIO(JTDI,           PIN(A, 15), GPIO_DEFAULT)
GPIO(JTDO,           PIN(B, 3),  GPIO_DEFAULT)
GPIO(JNTRST,         PIN(B, 4),  GPIO_DEFAULT)
GPIO(OSC32_OUT,      PIN(C, 15), GPIO_DEFAULT)
#endif
