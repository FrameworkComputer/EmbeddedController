/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <logging/log.h>
#include "usbc/ppc.h"
#include "hooks.h"
#include "cros_board_info.h"

LOG_MODULE_REGISTER(alt_dev_replacement);

#define BOARD_VERSION_UNKNOWN 0xffffffff

/* Check board version to decide which ppc is used. */
static bool board_has_syv_ppc(void)
{
	static uint32_t board_version = BOARD_VERSION_UNKNOWN;

	if (board_version == BOARD_VERSION_UNKNOWN) {
		if (cbi_get_board_version(&board_version) != EC_SUCCESS) {
			LOG_ERR("Failed to get board version.");
			board_version = 0;
		}
	}

	return (board_version >= 1);
}

static int check_alternate_devices(const struct device *unused)
{
	ARG_UNUSED(unused);
	/* Configure the PPC driver */
	if (board_has_syv_ppc())
		PPC_ENABLE_ALTERNATE(ppc_port0_syv);

	return 0;
}
SYS_INIT(check_alternate_devices, APPLICATION, HOOK_PRIO_DEFAULT);
