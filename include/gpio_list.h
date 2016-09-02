/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "gpio.h"
#include "gpio_signal.h"

#ifdef CONFIG_COMMON_GPIO_SHORTNAMES
#define GPIO(name, pin, flags) {GPIO_NAME_BY_##pin, GPIO_##pin, flags},
#else
#define GPIO(name, pin, flags) {#name, GPIO_##pin, flags},
#endif

#define UNIMPLEMENTED(name) {#name, DUMMY_GPIO_BANK, 0, GPIO_DEFAULT},
#define GPIO_INT(name, pin, flags, signal) GPIO(name, pin, flags)

/* GPIO signal list. */
const struct gpio_info gpio_list[] = {
	#include "gpio.wrap"
};

BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* GPIO Interrupt Handlers */
#define GPIO_INT(name, pin, flags, signal) signal,
void (* const gpio_irq_handlers[])(enum gpio_signal signal) = {
	#include "gpio.wrap"
};
const int gpio_ih_count = ARRAY_SIZE(gpio_irq_handlers);

/*
 * ALL GPIOs with interrupt handlers must be declared at the top of the gpio.inc
 * file.
 */
#define GPIO_INT(name, pin, flags, signal)	\
	BUILD_ASSERT(GPIO_##name < ARRAY_SIZE(gpio_irq_handlers));
#include "gpio.wrap"

/*
 * All PIN(...) assignments must be unique, since otherwise you're just
 * creating duplicate names for the same thing, and may not always be
 * initializing them the way you think.
 */
#define GPIO(name, pin, flags) pin
#define GPIO_INT(name, pin, flags, signal) pin
/*
 * The compiler will complain if we use the same name twice. The linker ignores
 * anything that gets by.
 */
#define PIN(a, b...) static const int _pin_ ## a ## _ ## b \
	__attribute__((unused, section(".unused"))) = __LINE__;
#include "gpio.wrap"
