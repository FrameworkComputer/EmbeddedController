/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
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
GPIO(KB_IN01, PIN(0, 23), GPIO_KB_INPUT)
GPIO(KB_IN02, PIN(0, 1),  GPIO_KB_INPUT)
GPIO(KB_IN03, PIN(0, 4),  GPIO_KB_INPUT)
GPIO(KB_IN04, PIN(0, 0),  GPIO_KB_INPUT)
GPIO(KB_IN05, PIN(0, 29), GPIO_KB_INPUT)
GPIO(KB_IN06, PIN(0, 22), GPIO_KB_INPUT)
GPIO(KB_IN07, PIN(0, 25), GPIO_KB_INPUT)

/* Other inputs */
GPIO(LID_PRESENT_L, PIN(0, 30), GPIO_INPUT)    /* Hall sensor */

/* Useful for test software */
GPIO(IND_CHRG_DISABLE, PIN(0, 20), GPIO_INPUT)

/* Outputs */
GPIO(KB_OUT00, PIN(0, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01, PIN(0, 10), GPIO_KB_OUTPUT)
GPIO(KB_OUT02, PIN(0, 7),  GPIO_KB_OUTPUT)
GPIO(KB_OUT03, PIN(0, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT04, PIN(0, 3),  GPIO_KB_OUTPUT)
GPIO(KB_OUT05, PIN(0, 9),  GPIO_KB_OUTPUT)
GPIO(KB_OUT06, PIN(0, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07, PIN(0, 27), GPIO_KB_OUTPUT)
GPIO(KB_OUT08, PIN(0, 18), GPIO_KB_OUTPUT)
GPIO(KB_OUT09, PIN(0, 16), GPIO_KB_OUTPUT)
GPIO(KB_OUT10, PIN(0, 12), GPIO_KB_OUTPUT)
GPIO(KB_OUT11, PIN(0, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT12, PIN(0, 11), GPIO_KB_OUTPUT)

/* SPI */
GPIO(MCU_SPI_MOSI, PIN(0, 13), GPIO_OUTPUT)
GPIO(MCU_SPI_MISO, PIN(0, 14), GPIO_INPUT)
GPIO(MCU_SPI_SCLK, PIN(0, 17), GPIO_OUTPUT)
GPIO(MCU_SPI_CS_L, PIN(0, 19), GPIO_OUT_HIGH)

/* VBATT_SENSE */
GPIO(VBATT_SENSE, PIN(0, 26), GPIO_ANALOG)
GPIO(VBATT_SENSE_EN, PIN(0, 21), GPIO_OUT_LOW)

/* Unimplemented */
UNIMPLEMENTED(ENTERING_RW)
