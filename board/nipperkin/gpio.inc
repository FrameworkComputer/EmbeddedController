/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the board GPIOs that we care about. */

#include "base_gpio.inc"

/* LED Signals */
ALTERNATE(/*EC_PWM_LED_CHRG_L*/	PIN_MASK(C, BIT(4)), 0, 			MODULE_PWM, 0)		/* Charging LED */
ALTERNATE(/*EC_PWM_LED_FULL_L*/	PIN_MASK(8, BIT(0)), 0, 			MODULE_PWM, 0)		/* Full LED */

/* Test Points */
GPIO(EC_GPIO56,			PIN(5, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_PS2_CLK,		PIN(6, 7), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_PS2_DAT,		PIN(7, 0), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_PS2_RST,		PIN(6, 2), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_GPIOB0,			PIN(B, 0), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_GPIO81,			PIN(8, 1), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_FLPRG2,			PIN(8, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_PSL_GPO,		PIN(D, 7), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EC_PWM7,			PIN(6, 0), GPIO_INPUT | GPIO_PULL_UP)