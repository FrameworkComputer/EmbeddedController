/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LCOV_EXCL_START */
/* This is test code, so it should be excluded from coverage */

#include "common.h"
#include "drivers/cros_system.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

/* Stubbed cros_system API */

test_mockable int cros_system_get_reset_cause(void)
{
	return 0;
}

test_mockable int cros_system_soc_reset(void)
{
	return 0;
}

test_mockable int cros_system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	ARG_UNUSED(seconds);
	ARG_UNUSED(microseconds);

	return 0;
}

test_mockable const char *cros_system_chip_vendor(void)
{
	return "NATIVE_POSIX_VENDOR";
}

test_mockable const char *cros_system_chip_name(void)
{
	return "NATIVE_POSIX_CHIP";
}

test_mockable const char *cros_system_chip_revision(void)
{
	return "NATIVE_POSIX_REVISION";
}

#ifdef CONFIG_PM
test_mockable uint64_t cros_system_deep_sleep_ticks(void)
{
	return 0;
}
#endif

static int cros_system_native_posix_init(void)
{
	return 0;
}
SYS_INIT(cros_system_native_posix_init, PRE_KERNEL_1,
	 CONFIG_CROS_SYSTEM_INIT_PRIORITY);

/* LCOV_EXCL_STOP */
