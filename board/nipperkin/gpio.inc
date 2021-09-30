/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the board GPIOs that we care about. */

#include "base_gpio.inc"

/* LED Signals */
GPIO(C0_CHARGE_LED_AMBER_L,		PIN(C, 4), GPIO_OUT_HIGH)
GPIO(C0_CHARGE_LED_WHITE_L,		PIN(8, 0), GPIO_OUT_HIGH)
GPIO(C1_CHARGE_LED_AMBER_L,		PIN(6, 7), GPIO_OUT_HIGH)
GPIO(C1_CHARGE_LED_WHITE_L,		PIN(7, 0), GPIO_OUT_HIGH)

/* HDMI */
IOEX(EN_PWR_HDMI,		EXPIN(USBC_PORT_C0, 0, 3), GPIO_OUT_LOW)
IOEX(HDMI_DATA_EN,		EXPIN(USBC_PORT_C0, 1, 4), GPIO_OUT_LOW)
GPIO_INT(HPD_EC_IN,		PIN(6, 2), GPIO_INT_BOTH, hdmi_hpd_interrupt)

/* Test Points */
GPIO(EC_GPIO56,			PIN(5, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_GPIOB0,			PIN(B, 0), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_GPIO81,			PIN(8, 1), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_FLPRG2,			PIN(8, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_PSL_GPO,		PIN(D, 7), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_PWM7,			PIN(6, 0), GPIO_INPUT | GPIO_PULL_UP)
