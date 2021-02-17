/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_system.h>
#include <logging/log.h>

#include "system.h"

LOG_MODULE_REGISTER(shim_npcx_system, LOG_LEVEL_ERR);

void system_reset(int flags)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");
	int err;

	if (!sys_dev)
		LOG_ERR("sys_dev get binding failed");

	/*
	 * TODO(b/176523207): reset flag & SYSTEM_RESET_WAIT_EXT
	 */

	err = cros_system_soc_reset(sys_dev);

	if (err < 0)
		LOG_ERR("soc reset failed");

	while (1)
		;
}
