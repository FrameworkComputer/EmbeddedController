/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_GUREN_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_GUREN_H_

#include "ec_commands.h"

void fan_init(void);
void board_setup_init(void);
void alt_sensor_init(void);
void kb_init(void);

extern enum guren_sub_board_type guren_cached_sub_board;

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_GUREN_H_ */
