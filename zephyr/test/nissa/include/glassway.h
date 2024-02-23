/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_GLASSWAY_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_GLASSWAY_H_

#include "ec_commands.h"

void fan_init(void);

extern enum glassway_sub_board_type glassway_cached_sub_board;

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_GLASSWAY_H_ */
