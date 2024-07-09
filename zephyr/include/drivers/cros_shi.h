/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Chrome OS-specific API for Serial Host Interface (SHI)
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_SHI_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_SHI_H_

/**
 * @brief CROS Serial Host Interface Driver APIs
 * @defgroup cros_shi_interface CROS Serial Host Interface Driver APIs
 * @ingroup io_interfaces
 * @{
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

/**
 * @cond INTERNAL_HIDDEN
 *
 * cros Serial Host Interface driver API definition and system call entry points
 *
 * (Internal use only.)
 */
typedef int (*cros_shi_api_enable)(const struct device *dev);

typedef int (*cros_shi_api_disable)(const struct device *dev);

/** @brief Driver API structure. */
__subsystem struct cros_shi_driver_api {
	cros_shi_api_enable enable;
	cros_shi_api_disable disable;
};

/**
 * @brief Enable SHI module.
 *
 * @param dev Pointer to the device structure for the driver instance.
 *
 * @retval non-negative if successful.
 * @retval Negative errno code if failure.
 */
__syscall int cros_shi_enable(const struct device *dev);

static inline int z_impl_cros_shi_enable(const struct device *dev)
{
	const struct cros_shi_driver_api *api =
		(const struct cros_shi_driver_api *)dev->api;

	if (!api->enable) {
		return -ENOTSUP;
	}

	return api->enable(dev);
}

/**
 * @brief Disable SHI module.
 *
 * @param dev Pointer to the device structure for the driver instance.
 *
 * @retval no return if successful.
 * @retval Negative errno code if failure.
 */
__syscall int cros_shi_disable(const struct device *dev);

static inline int z_impl_cros_shi_disable(const struct device *dev)
{
	const struct cros_shi_driver_api *api =
		(const struct cros_shi_driver_api *)dev->api;

	if (!api->disable) {
		return -ENOTSUP;
	}

	return api->disable(dev);
}

/**
 * @}
 */
#include <zephyr/syscalls/cros_shi.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_SHI_H_ */
