/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"

#ifdef CONFIG_COMMON_GPIO_SHORTNAMES
#define GPIO(name, port, pin, flags)			\
	{#port#pin, GPIO_##port, (1 << pin), flags},
#else
#define GPIO(name, port, pin, flags)			\
	{#name, GPIO_##port, (1 << pin), flags},
#endif

#define UNIMPLEMENTED(name) \
	{#name, DUMMY_GPIO_BANK, 0, GPIO_DEFAULT},

#define GPIO_INT(name, port, pin, flags, signal)	\
	GPIO(name, port, pin, flags)
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

/* GPIO Interrupt Handlers */
#define GPIO_INT(name, port, pin, flags, signal) signal,
void (* const gpio_irq_handlers[])(enum gpio_signal signal) = {
	#include "gpio.wrap"
};
const int gpio_ih_count = ARRAY_SIZE(gpio_irq_handlers);

/*
 * ALL GPIOs with interrupt handlers must be declared at the top of the gpio.inc
 * file.
 */
#define GPIO_INT(name, port, pin, flags, signal)	\
	BUILD_ASSERT(GPIO_##name < ARRAY_SIZE(gpio_irq_handlers));
#include "gpio.wrap"
