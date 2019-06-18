/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(POWER_BUTTON_L, PIN(B, 5),  GPIO_INT_BOTH,                power_button_interrupt) /* wk6 */ /* active high, the name is for compatibility with existing code */
GPIO_INT(SOC_POWER_GOOD, PIN(A, 3),  GPIO_INT_BOTH,                power_signal_interrupt)
GPIO_INT(LID_OPEN,       PIN(C, 13), GPIO_INT_BOTH,                lid_interrupt)
GPIO_INT(SUSPEND_L,      PIN(C, 7),  GPIO_INT_BOTH,                power_signal_interrupt)
GPIO_INT(SPI1_NSS,       PIN(A, 4),  GPIO_INT_BOTH,                spi_event)
GPIO_INT(AC_PRESENT,     PIN(C, 6),  GPIO_INT_BOTH | GPIO_PULL_UP, extpower_interrupt)

/* Keyboard inputs */
#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

GPIO_INT(KB_IN00, PIN(C, 8),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN01, PIN(C, 9),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN02, PIN(C, 10), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN03, PIN(C, 11), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN04, PIN(C, 12), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN05, PIN(C, 14), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN06, PIN(C, 15), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN07, PIN(D, 2),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

/* Other inputs */
GPIO(EC_WAKE, PIN(A, 0), GPIO_INPUT | GPIO_PULL_DOWN) /* wk1 */
GPIO(WP_L,    PIN(B, 4), GPIO_INPUT)

/* Outputs */
GPIO(BAT_LED_RED,       PIN(B, 11), GPIO_OUT_HIGH)
GPIO(BAT_LED_GREEN,     PIN(A, 11), GPIO_OUT_HIGH)
GPIO(EC_BL_OVERRIDE,    PIN(F, 1),  GPIO_OUT_LOW)
GPIO(EC_INT_L,          PIN(B, 9),  GPIO_OUT_LOW)
GPIO(ENTERING_RW,       PIN(F, 0),  GPIO_OUT_LOW)
GPIO(I2C1_SCL,          PIN(B, 6),  GPIO_ODR_HIGH)
GPIO(I2C1_SDA,          PIN(B, 7),  GPIO_ODR_HIGH)
GPIO(KB_OUT00,          PIN(B, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,          PIN(B, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT02,          PIN(B, 12), GPIO_OUT_LOW) /* Inverted from silegro */
GPIO(KB_OUT03,          PIN(B, 13), GPIO_KB_OUTPUT)
GPIO(KB_OUT04,          PIN(B, 14), GPIO_KB_OUTPUT)
GPIO(KB_OUT05,          PIN(B, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT06,          PIN(C, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07,          PIN(C, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT08,          PIN(C, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,          PIN(B, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,          PIN(C, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT11,          PIN(C, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT12,          PIN(A, 13), GPIO_KB_OUTPUT)
GPIO(POWER_LED,         PIN(A, 2),  GPIO_OUT_HIGH)
GPIO(PMIC_PWRON,        PIN(A, 12), GPIO_OUT_LOW)
GPIO(PMIC_RESET,        PIN(B, 3),  GPIO_OUT_LOW)
GPIO(PMIC_SOURCE_PWREN, PIN(B, 10), GPIO_OUT_LOW)
GPIO(PMIC_WARM_RESET_L, PIN(C, 3),  GPIO_ODR_HIGH)

ALTERNATE(PIN_MASK(A, 0x00f0), 0, MODULE_SPI,  0)
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART, 0)
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,  0)
