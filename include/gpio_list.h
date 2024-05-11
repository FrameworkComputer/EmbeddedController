/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "gpio.h"
#include "gpio_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_COMMON_GPIO_SHORTNAMES
#define GPIO(name, pin, flags) { GPIO_NAME_BY_##pin, GPIO_##pin, flags },
#define GPIO_INT(name, pin, flags, signal) \
	{ GPIO_NAME_BY_##pin, GPIO_##pin, flags },
#else
#define GPIO(name, pin, flags) { #name, GPIO_##pin, flags },
#define GPIO_INT(name, pin, flags, signal) { #name, GPIO_##pin, flags },
#endif

#define UNIMPLEMENTED(name) { #name, UNIMPLEMENTED_GPIO_BANK, 0, GPIO_DEFAULT },

/* GPIO signal list. */
__const_data const struct gpio_info gpio_list[] = {
#include "gpio.wrap"
};

BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

#define UNUSED(pin) { GPIO_##pin },
/* Unconnected pin list. */
__const_data const struct unused_pin_info unused_pin_list[] = {
#include "gpio.wrap"
};

const int unused_pin_count = ARRAY_SIZE(unused_pin_list);

/* GPIO Interrupt Handlers */
#define GPIO_INT(name, pin, flags, signal) signal,
void (*const gpio_irq_handlers[])(enum gpio_signal signal) = {
#include "gpio.wrap"
};
const int gpio_ih_count = ARRAY_SIZE(gpio_irq_handlers);

/*
 * ALL GPIO_INTs must appear before GPIOs (from gpio.wrap).
 * This is because the enum gpio_signal names are used to index into
 * the gpio_irq_handlers array.
 *
 * This constraint is handled within gpio.wrap.
 */
#define GPIO_INT(name, pin, flags, signal) \
	BUILD_ASSERT(GPIO_##name < ARRAY_SIZE(gpio_irq_handlers));
#include "gpio.wrap"

/*
 * All PIN(...) assignments must be unique, since otherwise you're just
 * creating duplicate names for the same thing, and may not always be
 * initializing them the way you think.
 */
#define GPIO(name, pin, flags) pin
#define GPIO_INT(name, pin, flags, signal) pin
#define UNUSED(pin) pin
/*
 * Check at build time that pin/ports are only defined once.
 * The compiler will complain if we use the same name twice. The linker ignores
 * anything that gets by.
 */
#define PIN(a, b...)                    \
	static const int _pin_##a##_##b \
		__attribute__((unused, section(".unused"))) = __LINE__;
#include "ioexpander.h"

#include "gpio.wrap"
#define IOEX_EXPIN(ioex, port, index) (ioex), (port), BIT(index)

/*
 *  Define the IO expander IO in gpio.inc by the format:
 *    IOEX(name, EXPIN(ioex_port, port, offset), flags)
 *      - name: the name of this IO pin
 *      - EXPIN(ioex, port, offset)
 *         - ioex: the IO expander port (defined in board.c) this IO
 *                 pin belongs to.
 *         - port: the port number in the IO expander chip.
 *         - offset: the bit offset in the port above.
 *      - flags: the same as the flags of GPIO.
 *
 */
#define IOEX(name, expin, flags) { #name, IOEX_##expin, flags },
/*
 *  Define the IO expander IO which supports interrupt in gpio.inc by
 *  the format:
 *    IOEX_INT(name, EXPIN(ioex_port, port, offset), flags, handler)
 *      - name: the name of this IO pin
 *      - EXPIN(ioex, port, offset)
 *         - ioex: the IO expander port (defined in board.c) this IO
 *                 pin belongs to.
 *         - port: the port number in the IO expander chip.
 *         - offset: the bit offset in the port above.
 *      - flags: the same as the flags of GPIO.
 *      - handler: the IOEX IO's interrupt handler.
 */
#define IOEX_INT(name, expin, flags, handler) { #name, IOEX_##expin, flags },

/* IO expander signal list. */
const struct ioex_info ioex_list[] = {
#include "gpio.wrap"
};
BUILD_ASSERT(ARRAY_SIZE(ioex_list) == IOEX_COUNT);

/* IO Expander Interrupt Handlers */
#define IOEX_INT(name, expin, flags, handler) handler,
void (*const ioex_irq_handlers[])(enum ioex_signal signal) = {
#include "gpio.wrap"
};
const int ioex_ih_count = ARRAY_SIZE(ioex_irq_handlers);
/*
 * All IOEX IOs with interrupt handlers must be declared at the top of the
 * IOEX's declaration in the gpio.inc
 * file.
 */
#define IOEX_INT(name, expin, flags, handler)          \
	BUILD_ASSERT(IOEX_##name - IOEX_SIGNAL_START < \
		     ARRAY_SIZE(ioex_irq_handlers));
#include "gpio.wrap"

#define IOEX(name, expin, flags) expin
#define IOEX_INT(name, expin, flags, handler) expin

/* The compiler will complain if we use the same name twice or the controller
 * number declared is greater or equal to CONFIG_IO_EXPANDER_PORT_COUNT.
 * The linker ignores anything that gets by.
 */
#define EXPIN(a, b, c...)                                               \
	static const int _expin_##a##_##b##_##c                         \
		__attribute__((unused, section(".unused"))) = __LINE__; \
	BUILD_ASSERT(a < CONFIG_IO_EXPANDER_PORT_COUNT);

#ifdef __cplusplus
}
#endif

#include "gpio.wrap"
