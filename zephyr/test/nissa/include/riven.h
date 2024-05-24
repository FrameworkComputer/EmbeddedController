/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_RIVEN_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_RIVEN_H_

#include "ec_commands.h"

extern const struct ec_response_keybd_config craask_kb;
extern const struct ec_response_keybd_config craask_kb_w_kb_numpad;

extern const struct batt_conf_embed *battery_conf;
extern int charge_port;

void clamshell_init(void);
void fan_init(void);
void thermal_init(void);

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_RIVEN_H_ */
