/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_YAVIKS_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_YAVIKS_H_

void kb_layout_init(void);
void fan_init(void);

extern enum yaviks_sub_board_type yaviks_cached_sub_board;

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_YAVIKS_H_ */
