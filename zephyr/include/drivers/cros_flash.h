/*
 * Copyright 2020 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Chrome OS-specific API for flash memory access
 * This exists only support the interface expected by the Chrome OS EC. It seems
 * better to implement this so we can make use of most of the existing code in
 * its keyboard_scan.c file and thus make sure we operate the same way.
 *
 * It provides raw access to flash memory module.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_FLASH_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_FLASH_H_

#include <kernel.h>
#include <device.h>

/**
 * @brief CROS Flash Driver APIs
 * @defgroup cros_flash_interface CROS Flash Driver APIs
 * @ingroup io_interfaces
 * @{
 */

/**
 * @cond INTERNAL_HIDDEN
 *
 * cros keyboard raw driver API definition and system call entry points
 *
 * (Internal use only.)
 */
typedef int (*cros_flash_api_init)(const struct device *dev);

typedef int (*cros_flash_api_physical_read)(const struct device *dev,
					    int offset, int size, char *data);

typedef int (*cros_flash_api_physical_write)(const struct device *dev,
					     int offset, int size,
					     const char *data);

typedef int (*cros_flash_api_physical_erase)(const struct device *dev,
					     int offset, int size);

__subsystem struct cros_flash_driver_api {
	cros_flash_api_init init;
	cros_flash_api_physical_read physical_read;
	cros_flash_api_physical_write physical_write;
	cros_flash_api_physical_erase physical_erase;
};

/**
 * @endcond
 */

/**
 * @brief Initialize physical flash.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_init(const struct device *dev);

static inline int z_impl_cros_flash_init(const struct device *dev)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->init) {
		return -ENOTSUP;
	}

	return api->init(dev);
}

/**
 * @brief Read from physical flash.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param offset Flash offset to read.
 * @param size Number of bytes to read.
 * @param data Destination buffer for data.  Must be 32-bit aligned.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_physical_read(const struct device *dev, int offset,
				       int size, char *data);

static inline int z_impl_cros_flash_physical_read(const struct device *dev,
						  int offset, int size,
						  char *data)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_read) {
		return -ENOTSUP;
	}

	return api->physical_read(dev, offset, size, data);
}

/**
 * @brief Write to physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param offset Flash offset to write.
 * @param size Number of bytes to write.
 * @param data Destination buffer for data.  Must be 32-bit aligned.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_physical_write(const struct device *dev, int offset,
					int size, const char *data);

static inline int z_impl_cros_flash_physical_write(const struct device *dev,
						   int offset, int size,
						   const char *data)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_write) {
		return -ENOTSUP;
	}

	return api->physical_write(dev, offset, size, data);
}

/**
 * @brief Erase physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param offset	Flash offset to erase.
 * @param size	        Number of bytes to erase.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_physical_erase(const struct device *dev, int offset,
					int size);

static inline int z_impl_cros_flash_physical_erase(const struct device *dev,
						   int offset, int size)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_erase) {
		return -ENOTSUP;
	}

	return api->physical_erase(dev, offset, size);
}

/**
 * @}
 */
#include <syscalls/cros_flash.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_FLASH_H_ */
