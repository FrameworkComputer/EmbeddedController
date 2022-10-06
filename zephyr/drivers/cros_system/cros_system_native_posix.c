/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LCOV_EXCL_START */
/* This is test code, so it should be excluded from coverage */

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "common.h"
#include "drivers/cros_system.h"

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

/* Driver config stub */
struct cros_system_native_posix_config {};

/* Driver data stub */
struct cros_system_native_posix_data {};

test_mockable_static int cros_system_native_posix_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

/* Stubbed cros_system API */

test_mockable_static int
cros_system_native_posix_get_reset_cause(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

test_mockable_static int
cros_system_native_posix_soc_reset(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

test_mockable_static int
cros_system_native_posix_hibernate(const struct device *dev, uint32_t seconds,
				   uint32_t microseconds)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(seconds);
	ARG_UNUSED(microseconds);

	return 0;
}

test_mockable_static const char *
cros_system_native_posix_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "NATIVE_POSIX_VENDOR";
}

test_mockable_static const char *
cros_system_native_posix_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "NATIVE_POSIX_CHIP";
}

test_mockable_static const char *
cros_system_native_posix_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "NATIVE_POSIX_REVISION";
}

__maybe_unused test_mockable_static uint64_t
cros_system_native_posix_deep_sleep_ticks(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static struct cros_system_native_posix_data cros_system_native_posix_dev_data;

static const struct cros_system_native_posix_config cros_system_dev_cfg = {};

/* clang-format off */
/* clang-format extends past 80 lines here */
static const struct
cros_system_driver_api cros_system_driver_native_posix_api = {
	.get_reset_cause = cros_system_native_posix_get_reset_cause,
	.soc_reset = cros_system_native_posix_soc_reset,
	.hibernate = cros_system_native_posix_hibernate,
	.chip_vendor = cros_system_native_posix_get_chip_vendor,
	.chip_name = cros_system_native_posix_get_chip_name,
	.chip_revision = cros_system_native_posix_get_chip_revision,
#ifdef CONFIG_PM
	.deep_sleep_ticks = cros_system_native_posix_deep_sleep_ticks,
#endif
};
/* clang-format on */

DEVICE_DEFINE(cros_system_native_posix_0, "CROS_SYSTEM",
	      cros_system_native_posix_init, NULL,
	      &cros_system_native_posix_dev_data, &cros_system_dev_cfg,
	      PRE_KERNEL_1, CONFIG_CROS_SYSTEM_NATIVE_POSIX_INIT_PRIORITY,
	      &cros_system_driver_native_posix_api);

/* LCOV_EXCL_STOP */
