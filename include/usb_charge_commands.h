/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control commands for Chrome EC */

#ifndef __CROS_EC_USB_CHARGE_COMMANDS_H
#define __CROS_EC_USB_CHARGE_COMMANDS_H

#include "common.h"

/* Initializes the module. */
int usb_charge_commands_init(void);

/* Host command handlers. */
enum lpc_status usb_charge_command_set_mode(uint8_t *data);

#endif  /* __CROS_EC_USB_CHARGE_COMMANDS_H */
