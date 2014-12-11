/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef CONFIG_COMMON_GPIO_SHORTNAMES
#define GPIO(name, port, pin, flags, signal) \
	{#port#pin, GPIO_##port, (1 << pin), flags, signal},
#else
#define GPIO(name, port, pin, flags, signal) \
	{#name, GPIO_##port, (1 << pin), flags, signal},
#endif

#define UNIMPLEMENTED(name) \
	{#name, DUMMY_GPIO_BANK, 0, GPIO_DEFAULT, NULL},

/* GPIO signal list. */
const struct gpio_info gpio_list[] = {
	#include "gpio.wrap"
};

BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/*
 * Construct the gpio_alt_funcs array.  This array is used by gpio_config_module
 * to enable and disable GPIO alternate functions on a module by module basis.
 */
#define ALTERNATE(port, mask, function, module, flags)	\
	{GPIO_##port, mask, function, module, flags},

const struct gpio_alt_func gpio_alt_funcs[] = {
	#include "gpio.wrap"
};

const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);
