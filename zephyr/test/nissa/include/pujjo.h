/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_PUJJO_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_PUJJO_H_

#include "ec_commands.h"

extern const struct ec_response_keybd_config pujjo_kb;

void fan_init(void);
void form_factor_init(void);

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_PUJJO_H_ */
