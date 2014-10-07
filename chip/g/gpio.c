/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"

/* TODO(crosbug.com/p/33432): We don't have any GPIOs defined yet! */

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
}

void gpio_pre_init(void)
{
}

static void gpio_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);
