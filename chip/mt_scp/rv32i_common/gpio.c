/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module */

#include "gpio.h"

void gpio_pre_init(void)
{
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
}
