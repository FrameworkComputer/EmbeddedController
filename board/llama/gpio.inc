/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(AC_PRESENT,       PIN(A, 0),  GPIO_INT_BOTH,                extpower_interrupt)     /* AC power present: PA 0 */
GPIO_INT(POWER_BUTTON_L,   PIN(B, 5),  GPIO_INT_BOTH | GPIO_PULL_UP, power_button_interrupt) /* Keyboard power button: PB 5 */
GPIO_INT(SOC_POWER_GOOD_L, PIN(A, 3),  GPIO_INT_BOTH,                power_signal_interrupt)
GPIO_INT(LID_OPEN,         PIN(C, 13), GPIO_INT_BOTH,                lid_interrupt)          /* LID switch detection */
GPIO_INT(SUSPEND_L,        PIN(C, 7),  GPIO_INT_BOTH,                power_signal_interrupt) /* AP suspend/resume state */

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

/* SPI Chip Select */
GPIO_INT(SPI1_NSS,       PIN(A, 4),  GPIO_INT_BOTH | GPIO_PULL_UP, spi_event)

GPIO(5V_GOOD_L,        PIN(A, 1),  GPIO_INT_BOTH)
GPIO(AP_RESET_H,       PIN(B, 3),  GPIO_OUT_LOW)
GPIO(BOARD_VERSION1,   PIN(C, 6),  GPIO_INPUT|GPIO_PULL_UP) /* Board version stuffing resistor 1 */


/* Outputs */
GPIO(BAT_LED0,          PIN(B, 11), GPIO_OUT_LOW)   /* LED_GREEN */
GPIO(BAT_LED1,          PIN(A, 11), GPIO_OUT_LOW)   /* LED_ORANGE */
GPIO(EC_BL_OVERRIDE,    PIN(F, 1),  GPIO_OUT_LOW)
GPIO(EC_INT_L,          PIN(B, 9),  GPIO_OUT_HIGH)
GPIO(ENTERING_RW,       PIN(F, 0),  GPIO_OUT_LOW)
GPIO(KB_OUT00,          PIN(B, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,          PIN(B, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT02,          PIN(B, 12), GPIO_OUT_LOW)   /* KSO2 is inverted */
GPIO(KB_OUT03,          PIN(B, 13), GPIO_KB_OUTPUT)
GPIO(KB_OUT04,          PIN(B, 14), GPIO_KB_OUTPUT)
GPIO(KB_OUT05,          PIN(B, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT06,          PIN(C, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07,          PIN(C, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT08,          PIN(C, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,          PIN(B, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,          PIN(C, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT11,          PIN(C, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT12,          PIN(A, 14), GPIO_KB_OUTPUT)
GPIO(SYSTEM_POWER_H,    PIN(B, 10), GPIO_OUT_LOW)
GPIO(PMIC_PWRON_H,      PIN(A, 12), GPIO_OUT_LOW)
GPIO(PMIC_WARM_RESET_H, PIN(C, 3),  GPIO_ODR_LOW)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,       PIN(B, 6),  GPIO_INPUT)
GPIO(MASTER_I2C_SDA,       PIN(B, 7),  GPIO_INPUT)

/* Unimplemented signals which we need to emulate for now */
/* UNIMPLEMENTED(ENTERING_RW) */
UNIMPLEMENTED(WP_L)
UNIMPLEMENTED(BOARD_VERSION2)
UNIMPLEMENTED(BOARD_VERSION3)
UNIMPLEMENTED(5V_POWER_GOOD)
UNIMPLEMENTED(AP_RESET_L)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   0) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,    0) /* I2C MASTER:PB6/7 */
ALTERNATE(PIN_MASK(A, 0x00f0), 0, MODULE_SPI,    0) /* SPI SLAVE:PA4/5/6/7 */
