/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"

#include "cros_board_info.h"
#include "hooks.h"

static int cros_cbi_ec_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	cros_cbi_ssfc_init();
	cros_cbi_fw_config_init();

	return 0;
}
SYS_INIT(cros_cbi_ec_init, APPLICATION, HOOK_PRIO_FIRST);
