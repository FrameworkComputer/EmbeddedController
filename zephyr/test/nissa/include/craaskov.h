/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_CRAASKOV_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_CRAASKOV_H_

#include "ec_commands.h"

extern int battery_fuel_gauge_type_override;
extern int charge_port;
extern const struct ec_response_keybd_config craaskov_kb;

void kb_layout_init(void);

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_CRAASKOV_H_ */
