/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ROACH_CMDS_H
#define ROACH_CMDS_H

enum RoachCommand {
	ROACH_CMD_KEYBOARD_MATRIX,
	ROACH_CMD_TOUCHPAD_REPORT,
	ROACH_CMD_SUSPEND,
	ROACH_CMD_RESUME,
	ROACH_CMD_UPDATER_COMMAND,
};

#endif
