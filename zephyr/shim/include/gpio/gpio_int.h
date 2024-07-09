/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_GPIO_GPIO_INT_H_
#define ZEPHYR_SHIM_INCLUDE_GPIO_GPIO_INT_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Zephyr based interrupt handling.
 * Uses device tree to configure the interrupt handling.
 */

/*
 * Creates an internal name for the interrupt config block.
 */
#define GPIO_INT_FROM_NODE(id) DT_CAT(gpio_interrupt_, id)

/*
 * Maps nodelabel of interrupt node to internal configuration block.
 */
#define GPIO_INT_FROM_NODELABEL(lbl) (GPIO_INT_FROM_NODE(DT_NODELABEL(lbl)))

/*
 * Unique enum name for the interrupt.
 */
#define GPIO_INT_ENUM(id) DT_CAT(INT_ENUM_, id)

/*
 * Create an enum list of the interrupts
 */
enum gpio_interrupts {
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
	DT_FOREACH_CHILD_STATUS_OKAY_SEP(
		DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_gpio_interrupts),
		GPIO_INT_ENUM, (, )),
#endif
	GPIO_INT_COUNT
};

/*
 * Forward reference to avoiding exposing internal structure
 * defined in gpio_int.c
 */
struct gpio_int_config;

/*
 * Enable the interrupt.
 *
 * Interrupts are not automatically enabled, so
 * each interrupt will need a call to activate the interrupt e.g
 *
 *   ... // set up device
 *   gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(my_interrupt_node));
 */
int gpio_enable_dt_interrupt(const struct gpio_int_config *const ic);

/*
 * Disable the interrupt.
 */
int gpio_disable_dt_interrupt(const struct gpio_int_config *const ic);

/*
 * Get the interrupt config for this interrupt.
 */
const struct gpio_int_config *
gpio_interrupt_get_config(enum gpio_interrupts intr);

/*
 * Declare interrupt configuration data structures.
 */
#define GPIO_INT_DECLARE(id) \
	extern const struct gpio_int_config *const GPIO_INT_FROM_NODE(id);

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
DT_FOREACH_CHILD(DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_gpio_interrupts),
		 GPIO_INT_DECLARE)
#endif

#undef GPIO_INT_DECLARE
#undef GPIO_INT_DECLARE_NODE

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SHIM_INCLUDE_GPIO_GPIO_INT_H_ */
