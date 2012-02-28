/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistent storage commands for Chrome EC */

#ifndef __CROS_EC_PSTORE_COMMANDS_H
#define __CROS_EC_PSTORE_COMMANDS_H

#include "common.h"

/* Host command handlers. */
enum lpc_status pstore_command_get_info(uint8_t *data);
enum lpc_status pstore_command_read(uint8_t *data);
enum lpc_status pstore_command_write(uint8_t *data);

#endif  /* __CROS_EC_PSTORE_COMMANDS_H */
