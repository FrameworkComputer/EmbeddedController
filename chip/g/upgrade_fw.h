/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_UPGRADE_FW_H
#define __EC_CHIP_G_UPGRADE_FW_H

#include <stddef.h>

/* TODO: Handle this in upgrade_fw.c, not usb_upgrade.c */
#define UPGRADE_DONE          0xB007AB1E

void fw_upgrade_command_handler(void *body,
				size_t cmd_size,
				size_t *response_size);

#endif  /* ! __EC_CHIP_G_UPGRADE_FW_H */
