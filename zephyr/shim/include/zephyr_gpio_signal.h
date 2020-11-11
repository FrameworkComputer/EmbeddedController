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

#define GPIO_SIGNAL(id) DT_CAT(GPIO_, id)
#define GPIO_SIGNAL_WITH_COMMA(id) GPIO_SIGNAL(id),
enum gpio_signal {
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

/*
 * While we don't support IO expanders at the moment, multiple
 * platform/ec headers (e.g., espi.h) require some of these constants
 * to be defined.  Define them as a compatibility measure.
 */
enum ioex_signal {
	IOEX_SIGNAL_START = GPIO_LIMIT + 1,
	IOEX_SIGNAL_END = IOEX_SIGNAL_START,
	IOEX_LIMIT = 0x1FFF,
};
BUILD_ASSERT(IOEX_SIGNAL_END < IOEX_LIMIT);

#define IOEX_COUNT (IOEX_SIGNAL_END - IOEX_SIGNAL_START)
