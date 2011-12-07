/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#ifndef __CROS_EC_HOST_COMMAND_H
#define __CROS_EC_HOST_COMMAND_H

#include "common.h"

/* Initializes the module. */
int host_command_init(void);

/* Called by LPC module when a command is written to port 66h. */
void host_command_received(int command);

#endif  /* __CROS_EC_HOST_COMMAND_H */
