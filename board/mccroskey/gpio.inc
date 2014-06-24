/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO(KB_IN00,        B, 8,  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN01,        B, 9,  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN02,        B, 10, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN03,        B, 11, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN04,        B, 12, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN05,        B, 13, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN06,        B, 14, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN07,        B, 15, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

/* TODO(crosbug.com/p/23494): interrupt handler for power button */
GPIO(KBD_PWR_BUTTON, B, 2,  GPIO_INPUT, NULL)

GPIO(OMZO_RDY_L,     A, 0,  GPIO_INPUT,    NULL) /* PA0_WKUP */
GPIO(OZMO_RST_L,     A, 2,  GPIO_ODR_HIGH, NULL)
GPIO(VBUS_UP_DET,    A, 3,  GPIO_INPUT,    NULL)
GPIO(OZMO_REQ_L,     A, 8,  GPIO_INPUT,    NULL)
GPIO(CHARGE_ZERO,    B, 0,  GPIO_INPUT,    NULL)
GPIO(CHARGE_SHUNT,   B, 1,  GPIO_INPUT,    NULL)
GPIO(PMIC_INT_L,     B, 5,  GPIO_INPUT,    NULL)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C1_SCL,       B, 6,  GPIO_INPUT, NULL)
GPIO(I2C1_SDA,       B, 7,  GPIO_INPUT, NULL)

GPIO(KB_OUT00,       C, 0,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT01,       C, 1,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT02,       C, 2,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT03,       C, 3,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT04,       C, 4,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT05,       C, 5,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT06,       C, 6,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT07,       C, 7,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT08,       C, 8,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT09,       C, 9,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT10,       C, 10, GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT11,       C, 11, GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT12,       C, 12, GPIO_KB_OUTPUT, NULL)
GPIO(USB_VBUS_CTRL,  C, 13, GPIO_OUT_LOW,   NULL)
GPIO(HUB_RESET,      C, 14, GPIO_ODR_HIGH,  NULL)
GPIO(WP_L,           D, 2,  GPIO_INPUT,     NULL)

/*
 * TODO(crosbug.com/p/23494): This will be an alternate function GPIO,
 * so remove it from here.
 */
GPIO(BL_PWM,         A, 1,  GPIO_OUTPUT, NULL)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(EC_INT)
UNIMPLEMENTED(ENTERING_RW)

#if 0
/* Other GPIOs (probably need to be set up below as alt. function) */
GPIO(STM_USBDM,      A, 11, GPIO_DEFAULT, NULL)
GPIO(STM_USBDP,      A, 12, GPIO_DEFAULT, NULL)
GPIO(JTMS_SWDIO,     A, 13, GPIO_DEFAULT, NULL)
GPIO(JTCK_SWCLK,     A, 14, GPIO_DEFAULT, NULL)
GPIO(JTDI,           A, 15, GPIO_DEFAULT, NULL)
GPIO(JTDO,           B, 3,  GPIO_DEFAULT, NULL)
GPIO(JNTRST,         B, 4,  GPIO_DEFAULT, NULL)
GPIO(OSC32_OUT,      C, 15, GPIO_DEFAULT, NULL)
#endif
