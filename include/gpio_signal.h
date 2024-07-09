/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GPIO_SIGNAL_H
#define __CROS_EC_GPIO_SIGNAL_H

#include "compile_time_macros.h"

#ifdef CONFIG_ZEPHYR
#include "zephyr_gpio_signal.h"
#else

#ifdef __cplusplus
extern "C" {
#endif

/*
 * There are 3 different IO signal types used by the EC.
 * Ensure they each use a unique range of values so we can tell them apart.
 * 1) Local GPIO => 0 to 0x0FFF
 * 2) IO expander GPIO => 0x1000 to 0x1FFF
 * 3) eSPI virtual wire signals (defined in include/espi.h) => 0x2000 to 0x2FFF
 */

#define GPIO(name, pin, flags) GPIO_##name,
#define UNIMPLEMENTED(name) GPIO_##name,
#define GPIO_INT(name, pin, flags, signal) GPIO_##name,

#define GPIO_SIGNAL_NONE -1 /* Invalid signal */
#define GPIO_SIGNAL_START 0 /* The first valid GPIO signal is 0 */

enum gpio_signal {
#include "gpio.wrap"
	GPIO_COUNT,
	/* Ensure that sizeof gpio_signal is large enough for ioex_signal */
	GPIO_LIMIT = 0x0FFF
};
BUILD_ASSERT(GPIO_COUNT < GPIO_LIMIT);

#define IOEX(name, expin, flags) IOEX_##name,
#define IOEX_INT(name, expin, flags, signal) IOEX_##name,

enum ioex_signal {
	/* The first valid IOEX signal is 0x1000 */
	IOEX_SIGNAL_START = GPIO_LIMIT + 1,
	/* Used to ensure that the first IOEX signal is same as start */
	__IOEX_PLACEHOLDER = GPIO_LIMIT,
#include "gpio.wrap"
	IOEX_SIGNAL_END,
	IOEX_LIMIT = 0x1FFF
};
BUILD_ASSERT(IOEX_SIGNAL_END < IOEX_LIMIT);

#define IOEX_COUNT (IOEX_SIGNAL_END - IOEX_SIGNAL_START)

#ifdef __cplusplus
}
#endif

#endif /* !CONFIG_ZEPHYR */

#endif /* __CROS_EC_GPIO_SIGNAL_H */
