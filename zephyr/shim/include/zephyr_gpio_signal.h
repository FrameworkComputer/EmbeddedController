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

#define GPIO_SIGNAL(id) DT_STRING_UPPER_TOKEN(id, enum_name)
#define GPIO_SIGNAL_WITH_COMMA(id) \
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name), (GPIO_SIGNAL(id), ), ())
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
