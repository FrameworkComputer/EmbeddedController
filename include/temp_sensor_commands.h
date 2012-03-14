/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor commands for Chrome EC */
/* This LPC command only serves as a workaround to provide reliable temperature
 * reading method until we solve the I2C hanging issue. Remove this when
 * possible. */

#ifndef __CROS_EC_TEMP_SENSOR_COMMANDS_H
#define __CROS_EC_TEMP_SENSOR_COMMANDS_H

#include "common.h"

/* Initializes the module. */
int temp_sensor_commands_init(void);

/* Host command handlers. */
enum lpc_status temp_sensor_command_get_readings(uint8_t *data);

#endif  /* __CROS_EC_TEMP_SENSOR_COMMANDS_H */
