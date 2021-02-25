/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_BBRAM_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_BBRAM_H_

#include <device.h>

/**
 * Check if "Invalid Battery-Backed RAM". This may occur as a result to low
 * voltage at the VBAT pin.
 *
 * @return 0 if the Battery-Backed RAM data is valid.
 */
typedef int (*cros_bbram_api_ibbr)(const struct device *dev);

/**
 * Reset the IBBR status (calling cros_bbram_ibbr will return 0 after this).
 *
 * @return 0 after reset is complete.
 * @see cros_bbram_ibbr
 */
typedef int (*cros_bbram_api_reset_ibbr)(const struct device *dev);

/**
 * Check for V SBY power failure. This will return an error if the V SBY power
 * domain is turned on after it was off.
 *
 * @return 0 if V SBY power domain is in normal operation.
 */
typedef int (*cros_bbram_api_vsby)(const struct device *dev);

/**
 * Reset the V SBY status (calling cros_bbram_vsby will return 0 after this).
 *
 * @return 0 after reset is complete.
 * @see cros_bbram_vsby
 */
typedef int (*cros_bbram_api_reset_vsby)(const struct device *dev);

/**
 * Check for V CC1 power failure. This will return an error if the V CC1 power
 * domain is turned on after it was off.
 *
 * @return 0 if the V CC1 power domain is in normal operation.
 */
typedef int (*cros_bbram_api_vcc1)(const struct device *dev);

/**
 * Reset the V CC1 status (calling cros_bbram_vcc1 will return 0 after this).
 *
 * @return 0 after reset is complete.
 * @see cros_bbram_vcc1
 */
typedef int (*cros_bbram_api_reset_vcc1)(const struct device *dev);

typedef int (*cros_bbram_api_read)(const struct device *dev, int offset,
				   int size, uint8_t *data);

typedef int (*cros_bbram_api_write)(const struct device *dev, int offset,
				    int size, uint8_t *data);

__subsystem struct cros_bbram_driver_api {
	cros_bbram_api_ibbr ibbr;
	cros_bbram_api_reset_ibbr reset_ibbr;
	cros_bbram_api_vsby vsby;
	cros_bbram_api_reset_vsby reset_vsby;
	cros_bbram_api_vcc1 vcc1;
	cros_bbram_api_reset_vcc1 reset_vcc1;
	cros_bbram_api_read read;
	cros_bbram_api_write write;
};

__syscall int cros_bbram_get_ibbr(const struct device *dev);

static inline int z_impl_cros_bbram_get_ibbr(const struct device *dev)
{
	const struct cros_bbram_driver_api *api =
		(const struct cros_bbram_driver_api *)dev->api;

	if (!api->ibbr) {
		return -ENOTSUP;
	}

	return api->ibbr(dev);
}

__syscall int cros_bbram_reset_ibbr(const struct device *dev);

static inline int z_impl_cros_bbram_reset_ibbr(const struct device *dev)
{
	const struct cros_bbram_driver_api *api =
		(const struct cros_bbram_driver_api *)dev->api;

	if (!api->reset_ibbr) {
		return -ENOTSUP;
	}

	return api->reset_ibbr(dev);
}

__syscall int cros_bbram_get_vsby(const struct device *dev);

static inline int z_impl_cros_bbram_get_vsby(const struct device *dev)
{
	const struct cros_bbram_driver_api *api =
		(const struct cros_bbram_driver_api *)dev->api;

	if (!api->vsby) {
		return -ENOTSUP;
	}

	return api->vsby(dev);
}

__syscall int cros_bbram_reset_vsby(const struct device *dev);

static inline int z_impl_cros_bbram_reset_vsby(const struct device *dev)
{
	const struct cros_bbram_driver_api *api =
		(const struct cros_bbram_driver_api *)dev->api;

	if (!api->reset_vsby) {
		return -ENOTSUP;
	}

	return api->reset_vsby(dev);
}

__syscall int cros_bbram_get_vcc1(const struct device *dev);

static inline int z_impl_cros_bbram_get_vcc1(const struct device *dev)
{
	const struct cros_bbram_driver_api *api =
		(const struct cros_bbram_driver_api *)dev->api;

	if (!api->vcc1) {
		return -ENOTSUP;
	}

	return api->vcc1(dev);
}

__syscall int cros_bbram_reset_vcc1(const struct device *dev);

static inline int z_impl_cros_bbram_reset_vcc1(const struct device *dev)
{
	const struct cros_bbram_driver_api *api =
		(const struct cros_bbram_driver_api *)dev->api;

	if (!api->reset_vcc1) {
		return -ENOTSUP;
	}

	return api->reset_vcc1(dev);
}

__syscall int cros_bbram_read(const struct device *dev, int offset, int size,
			      uint8_t *data);

static inline int z_impl_cros_bbram_read(const struct device *dev, int offset,
					 int size, uint8_t *data)
{
	const struct cros_bbram_driver_api *api =
		(const struct cros_bbram_driver_api *)dev->api;

	if (!api->read) {
		return -ENOTSUP;
	}

	return api->read(dev, offset, size, data);
}

__syscall int cros_bbram_write(const struct device *dev, int offset, int size,
			       uint8_t *data);

static inline int z_impl_cros_bbram_write(const struct device *dev, int offset,
					  int size, uint8_t *data)
{
	const struct cros_bbram_driver_api *api =
		(const struct cros_bbram_driver_api *)dev->api;

	if (!api->write) {
		return -ENOTSUP;
	}

	return api->write(dev, offset, size, data);
}

#include <syscalls/cros_bbram.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_BBRAM_H_ */
