/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM commands for Chrome EC */

#ifndef __CROS_EC_PWM_COMMANDS_H
#define __CROS_EC_PWM_COMMANDS_H

#include "common.h"
#include "lpc_commands.h"

/* Host command handlers. */
enum lpc_status pwm_command_get_fan_rpm(uint8_t *data);
enum lpc_status pwm_command_set_fan_target_rpm(uint8_t *data);


#endif  /* __CROS_EC_PWM_COMMANDS_H */
