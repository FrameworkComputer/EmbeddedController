/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_cbi.h>
#include <logging/log.h>
#include "hooks.h"

LOG_MODULE_REGISTER(shim_cbi, LOG_LEVEL_ERR);

static void cbi_dev_init(void)
{
	const struct device *dev = device_get_binding(CROS_CBI_LABEL);

	if (!dev)
		LOG_ERR("Fail to find %s", CROS_CBI_LABEL);

	cros_cbi_init(dev);
}

DECLARE_HOOK(HOOK_INIT, cbi_dev_init, HOOK_PRIO_FIRST);
