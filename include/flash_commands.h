/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory commands for Chrome EC */

#ifndef __CROS_EC_FLASH_COMMANDS_H
#define __CROS_EC_FLASH_COMMANDS_H

#include "common.h"
#include "lpc_commands.h"

/* Initializes the module. */
int flash_commands_init(void);

/* Host command handlers. */
enum lpc_status flash_command_get_info(uint8_t *data);
enum lpc_status flash_command_read(uint8_t *data);
enum lpc_status flash_command_write(uint8_t *data);
enum lpc_status flash_command_erase(uint8_t *data);

#endif  /* __CROS_EC_FLASH_COMMANDS_H */
