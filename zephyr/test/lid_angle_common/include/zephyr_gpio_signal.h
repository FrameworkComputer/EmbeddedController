/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file Since we don't actually have any GPIOs, just define the blank enum.
 */

#ifndef __ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_ZEPHYR_GPIO_SIGNAL_H
#define __ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_ZEPHYR_GPIO_SIGNAL_H

enum gpio_signal {
	GPIO_COUNT,
	GPIO_LIMIT = 0x0FFF,
};

#endif /* __ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_ZEPHYR_GPIO_SIGNAL_H */
