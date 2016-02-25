/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

/* Keyboard inputs */
/*
 * TODO(yjlou): call keyboard_raw_gpio_interrupt() in chip/nrf51/keyboard_raw.c
 */
GPIO(KB_IN00, PIN(0, 6),  GPIO_KB_INPUT)
GPIO(KB_IN01, PIN(0, 24), GPIO_KB_INPUT)
GPIO(KB_IN02, PIN(0, 1),  GPIO_KB_INPUT)
GPIO(KB_IN03, PIN(0, 4),  GPIO_KB_INPUT)
GPIO(KB_IN04, PIN(0, 0),  GPIO_KB_INPUT)
GPIO(KB_IN05, PIN(0, 30), GPIO_KB_INPUT)
GPIO(KB_IN06, PIN(0, 27), GPIO_KB_INPUT)
GPIO(KB_IN07, PIN(0, 26), GPIO_KB_INPUT)

/* Other inputs */
GPIO(MCU_GPIO_13,   PIN(0, 13), GPIO_INPUT)    /* PAIR on the debug board */
GPIO(MCU_GPIO_14,   PIN(0, 14), GPIO_INPUT)    /* TP 4 */
GPIO(MCU_GPIO_17,   PIN(0, 17), GPIO_INPUT)    /* TP 5 */
GPIO(MCU_GPIO_19,   PIN(0, 19), GPIO_INPUT)    /* TP 6 */
GPIO(BQ27621_GPOUT, PIN(0, 20), GPIO_INPUT)    /* Fuel Gauge */
GPIO(LID_PRESENT_L, PIN(0, 31), GPIO_INPUT)    /* Hall sensor */

/* Will be an output at some point */
GPIO(IND_CHRG_DISABLE, PIN(0, 21), GPIO_INPUT) /* Control for charging */

/* Outputs */
GPIO(KB_OUT00, PIN(0, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01, PIN(0, 10), GPIO_KB_OUTPUT)
GPIO(KB_OUT02, PIN(0, 7),  GPIO_KB_OUTPUT)
GPIO(KB_OUT03, PIN(0, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT04, PIN(0, 3),  GPIO_KB_OUTPUT)
GPIO(KB_OUT05, PIN(0, 9),  GPIO_KB_OUTPUT)
GPIO(KB_OUT06, PIN(0, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07, PIN(0, 28), GPIO_KB_OUTPUT)
GPIO(KB_OUT08, PIN(0, 18), GPIO_KB_OUTPUT)
GPIO(KB_OUT09, PIN(0, 16), GPIO_KB_OUTPUT)
GPIO(KB_OUT10, PIN(0, 12), GPIO_KB_OUTPUT)
GPIO(KB_OUT11, PIN(0, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT12, PIN(0, 11), GPIO_KB_OUTPUT)

/* Configure the TWI (I2C) interface in the init function. */
GPIO(MCU_SCL,      PIN(0, 23),  GPIO_INPUT | GPIO_PULL_UP | GPIO_OPEN_DRAIN)
GPIO(MCU_SDA,      PIN(0, 22),  GPIO_INPUT | GPIO_PULL_UP | GPIO_OPEN_DRAIN)

/* Use port 0 for I2C (TWI0), Pins 22 & 23 */
ALTERNATE(PIN_MASK(0, 0x800000), NRF51_TWI0_ALT_FUNC_SCL, MODULE_I2C,    GPIO_INPUT | GPIO_PULL_UP | GPIO_OPEN_DRAIN)
ALTERNATE(PIN_MASK(0, 0x400000), NRF51_TWI0_ALT_FUNC_SDA, MODULE_I2C,    GPIO_INPUT | GPIO_PULL_UP | GPIO_OPEN_DRAIN)

/* Unimplemented */
UNIMPLEMENTED(ENTERING_RW)
