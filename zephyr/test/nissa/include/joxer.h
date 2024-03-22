/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_JOXER_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_JOXER_H_

#include "ec_commands.h"

extern const struct ec_response_keybd_config joxer_kb_w_kb_light;
extern const struct ec_response_keybd_config joxer_kb_wo_kb_light;

void kb_layout_init(void);
void fan_init(void);
void form_factor_init(void);

extern enum joxer_sub_board_type joxer_cached_sub_board;

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_JOXER_H_ */
