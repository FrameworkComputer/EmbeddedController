/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(__CROS_EC_GPIO_SIGNAL_H) || defined(__CROS_EC_ZEPHYR_GPIO_SIGNAL_H)
#error "This file must only be included from gpio_signal.h. Include gpio_signal.h directly."
#endif
#define __CROS_EC_ZEPHYR_GPIO_SIGNAL_H

#include <devicetree.h>
#include <toolchain.h>

/** @brief Returns the enum-name property as a token
 *
 * Returns the enum-name property for this node as an upper case token
 * suitable for use as a GPIO signal name.
 * The enum-name property must exist, so this macro should only
 * be called conditionally upon checking the property exists.
 */
#define GPIO_SIGNAL_NAME_FROM_ENUM(id) DT_STRING_UPPER_TOKEN(id, enum_name)

/** @brief Creates a GPIO signal name using the DTS ordinal number
 *
 * Create a GPIO signal name for a GPIO that does not contain
 * the enum-name property. The DTS ordinal number is used
 * to generate a unique name for this GPIO.
 */
#define GPIO_SIGNAL_NAME_FROM_ORD(ord) DT_CAT(GPIO_ORD_, ord)

/** @brief Generate a GPIO signal name for this id
 *
 * Depending on whether the enum-name property exists, create
 * a GPIO signal name from either the enum-name or a
 * unique name generated using the DTS ordinal.
 */
#define GPIO_SIGNAL_NAME(id)					\
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name),		\
		(GPIO_SIGNAL_NAME_FROM_ENUM(id)),		\
		(GPIO_SIGNAL_NAME_FROM_ORD(id ## _ORD)))

#define GPIO_SIGNAL(id)		GPIO_SIGNAL_NAME(id)
#define GPIO_SIGNAL_WITH_COMMA(id) \
	GPIO_SIGNAL(id),
enum gpio_signal {
	GPIO_UNIMPLEMENTED = -1,
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
	DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_SIGNAL_WITH_COMMA)
#endif
	GPIO_COUNT,
	GPIO_LIMIT = 0x0FFF,
};
#undef GPIO_SIGNAL_WITH_COMMA

BUILD_ASSERT(GPIO_COUNT < GPIO_LIMIT);

/** @brief Converts a node identifier under named gpios to enum
 *
 * Converts the specified node identifier name, which should be nested under
 * the named_gpios node, into the correct enum gpio_signal that can be used
 * with platform/ec gpio API
 */
#define NAMED_GPIO(name) GPIO_SIGNAL(DT_PATH(named_gpios, name))

/** @brief Obtain a named gpio enum from a label and property
 *
 * Obtains a valid enum gpio_signal that can be used with platform/ec gpio API
 * from the property of a labeled node. The property has to point to a
 * named_gpios node.
 */
#define NAMED_GPIO_NODELABEL(label, prop) \
	GPIO_SIGNAL(DT_PHANDLE(DT_NODELABEL(label), prop))

/** @brief Converts a signal to a gpio_dt_spec pointer name.
 *
 * Prepend "DT_" to the the gpio_signal name to create a name that
 * can be used as a pointer to gpio_dt_spec.
 *
 * For example, given the DTS node under "named-gpios":
 *
 * gpio_ec_wp_l: ec_wp_l {
 *	gpio = <&gpioe 5 GPIO_INPUT>;
 *	enum-name = "GPIO_WP_L";
 * };
 *
 * aliases {
 *	other_name = &gpio_ec_wp_l;
 * };
 *
 * The following methods can all be used to access the GPIO:
 *
 * inp = gpio_get_level(GPIO_WP_L); // Legacy access
 * inp = gpio_pin_get_dt(DT_GPIO_LID_OPEN); // Zephyr API
 * inp = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_wp_l)); // Zephyr API
 * inp = gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(other_name));
 * enum gpio_signal sig = GPIO_WP_L;
 * inp = gpio_pin_get_dt(gpio_get_dt_spec(sig)); // Zephyr API
 *
 * DT_GPIO_LID_OPEN, GPIO_DT_FROM_NODELABEL and GPIO_DT_FROM_ALIAS will
 * resolve at build time, whereas gpio_get_dt_spec() will resolve at run-time.
 */
#define GPIO_DT_NAME(signal) DT_CAT(DT_, signal)

#define GPIO_DT_FROM_NODE(id) GPIO_DT_NAME(GPIO_SIGNAL(id))

#define GPIO_DT_FROM_ALIAS(id) GPIO_DT_FROM_NODE(DT_ALIAS(id))

#define GPIO_DT_FROM_NODELABEL(label) GPIO_DT_FROM_NODE(DT_NODELABEL(label))

#if DT_NODE_EXISTS(DT_PATH(named_gpios))
/*
 * Declare the pointers that refer to the gpio_dt_spec entries
 * for each GPIO.
 */
struct gpio_dt_spec;

#define GPIO_DT_PTR_DECL(id) extern const struct gpio_dt_spec * const \
	GPIO_DT_NAME(GPIO_SIGNAL(id));

DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_DT_PTR_DECL)

#undef GPIO_DT_PTR_DECL

#endif /* DT_NODE_EXISTS(DT_PATH(named_gpios)) */

/*
 * Define enums for IO expanders and signals
 */
#define IOEX_SIGNAL(id) DT_STRING_UPPER_TOKEN(id, enum_name)
#define IOEX_SIGNAL_WITH_COMMA(id) \
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name), (IOEX_SIGNAL(id), ), ())
enum ioex_signal {
	IOEX_SIGNAL_START = GPIO_LIMIT + 1,
	/* Used to ensure that the first IOEX signal is same as start */
	__IOEX_PLACEHOLDER = GPIO_LIMIT,
#if DT_NODE_EXISTS(DT_PATH(named_ioexes))
	DT_FOREACH_CHILD(DT_PATH(named_ioexes), IOEX_SIGNAL_WITH_COMMA)
#endif
	IOEX_SIGNAL_END,
	IOEX_LIMIT = 0x1FFF,
};
BUILD_ASSERT(IOEX_SIGNAL_END < IOEX_LIMIT);

#undef IOEX_SIGNAL_WITH_COMMA
#undef IOEX_SIGNAL

#define IOEX_COUNT (IOEX_SIGNAL_END - IOEX_SIGNAL_START)

#define IOEXPANDER_ID_EXPAND(id) ioex_chip_##id
#define IOEXPANDER_ID(id) IOEXPANDER_ID_EXPAND(id)
#define IOEXPANDER_ID_FROM_INST_WITH_COMMA(id) IOEXPANDER_ID(id),
enum ioexpander_id {
	DT_FOREACH_STATUS_OKAY(cros_ioex_chip,
		IOEXPANDER_ID_FROM_INST_WITH_COMMA)
	CONFIG_IO_EXPANDER_PORT_COUNT
};

#undef IOEXPANDER_ID_FROM_INST_WITH_COMMA
