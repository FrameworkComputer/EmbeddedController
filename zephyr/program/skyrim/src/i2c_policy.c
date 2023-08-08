/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"

int board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	/* Passthrough isn't used on Skyrim. */
	return 0;
}
