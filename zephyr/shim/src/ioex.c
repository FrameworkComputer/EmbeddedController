/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "common.h"
#ifdef __REQUIRE_ZEPHYR_GPIOS__
#undef __REQUIRE_ZEPHYR_GPIOS__
#endif
#include "ioexpander.h"

LOG_MODULE_REGISTER(ioex_shim, LOG_LEVEL_ERR);

#ifndef CONFIG_PLATFORM_EC_IOEX_CROS_DRV
/*
 * If no legacy cros-ec IOEX drivers are used, we need a stub
 * symbol for ioex_config[].  Set the IOEX_IS_CROS_DRV to constant 0
 * which will cause all these checks to compile out.
 */
struct ioexpander_config_t ioex_config[0];
#endif

int ioex_init(int ioex)
{
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_IOEX_CROS_DRV))
		return EC_SUCCESS;

	const struct ioexpander_drv *drv = ioex_config[ioex].drv;
	int rv;

	if (ioex_config[ioex].flags & IOEX_FLAGS_INITIALIZED)
		return EC_SUCCESS;

	if (drv->init != NULL) {
		rv = drv->init(ioex);
		if (rv != EC_SUCCESS)
			return rv;
	}

	ioex_config[ioex].flags |= IOEX_FLAGS_INITIALIZED;

	return EC_SUCCESS;
}

#ifdef CONFIG_PLATFORM_EC_IOEX_CROS_DRV
static int ioex_init_default(const struct device *unused)
{
	int ret;
	int i;

	ARG_UNUSED(unused);

	for (i = 0; i < CONFIG_IO_EXPANDER_PORT_COUNT; i++) {
		/* IO Expander has been initialized, skip re-initializing */
		if (ioex_config[i].flags & (IOEX_FLAGS_INITIALIZED |
					IOEX_FLAGS_DEFAULT_INIT_DISABLED))
			continue;

		ret = ioex_init(i);
		if (ret)
			LOG_ERR("Can't initialize ioex %d", i);
	}

	return 0;
}
SYS_INIT(ioex_init_default, POST_KERNEL, CONFIG_PLATFORM_EC_IOEX_INIT_PRIORITY);
#endif
