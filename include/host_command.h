/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#ifndef __CROS_EC_HOST_COMMAND_H
#define __CROS_EC_HOST_COMMAND_H

#include "common.h"

/* Called by LPC module when a command is written to one of the
   command slots (0=kernel, 1=user). */
void host_command_received(int slot, int command);

#endif  /* __CROS_EC_HOST_COMMAND_H */
