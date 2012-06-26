/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#ifndef __CROS_EC_VBOOT_H
#define __CROS_EC_VBOOT_H

#include "common.h"

/* Pre-initialize the module.  This occurs before clocks or tasks are set up. */
int vboot_pre_init(void);

/*
 * Check verified boot signatures, and jump to one of the RW images if
 * necessary.
 */
int vboot_check_signature(void);

/* Initialize the module. */
int vboot_init(void);

/* These are the vboot commands available via LPC. */
enum vboot_command {
	VBOOT_CMD_GET_FLAGS,
	VBOOT_CMD_SET_FLAGS,
	VBOOT_NUM_CMDS,
};

/*
 * These are the flags transferred across LPC. At the moment, only the devmode
 * flag can be set, and only because it's faked. Ultimately this functionality
 * will be moved elsewhere.
 */
#define VBOOT_FLAGS_IMAGE_MASK       0x03   /* enum system_image_copy_t */
#define VBOOT_FLAGS_FAKE_DEVMODE     0x04   /* fake dev-mode bit */

#endif  /* __CROS_EC_VBOOT_H */
