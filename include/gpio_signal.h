/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GPIO_SIGNAL_H
#define __CROS_EC_GPIO_SIGNAL_H

#define GPIO(name, port, pin, flags) GPIO_##name,
#define UNIMPLEMENTED(name) GPIO_##name,
#define GPIO_INT(name, port, pin, flags, signal) GPIO_##name,

enum gpio_signal {
	#include "gpio.wrap"
	GPIO_COUNT
};

#endif /* __CROS_EC_GPIO_SIGNAL_H */
