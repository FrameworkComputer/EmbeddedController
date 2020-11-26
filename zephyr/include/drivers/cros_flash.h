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
typedef int (*cros_flash_api_write_protection)(const struct device *dev,
					       bool enable);
typedef int (*cros_flash_api_write_protection_is_set)(const struct device *dev);
typedef int (*cros_flash_api_get_status_reg)(const struct device *dev,
					     char cmd_code, char *data);
typedef int (*cros_flash_api_set_status_reg)(const struct device *dev,
					     char *data);
typedef int (*cros_flash_api_uma_lock)(const struct device *dev, bool enable);

__subsystem struct cros_flash_driver_api {
	cros_flash_api_init init;
	cros_flash_api_physical_read physical_read;
	cros_flash_api_physical_write physical_write;
	cros_flash_api_physical_erase physical_erase;
	cros_flash_api_write_protection write_protection;
	cros_flash_api_write_protection_is_set write_protection_is_set;
	cros_flash_api_get_status_reg get_status_reg;
	cros_flash_api_set_status_reg set_status_reg;
	cros_flash_api_uma_lock uma_lock;
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
 * @brief Enable or disable write protection for a flash memory
 *
 * Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param enable True to enable it, False to disable it.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_write_protection_set(const struct device *dev,
					      bool enable);

static inline int
z_impl_cros_flash_write_protection_set(const struct device *dev, bool enable)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->write_protection) {
		return -ENOTSUP;
	}

	return api->write_protection(dev, enable);
}

/**
 * @brief Get write protection status of the flash device
 *
 * @return 1 If write protection is set, 0 otherwise.
 */
__syscall bool cros_flash_write_protection_is_set(const struct device *dev);

static inline bool
z_impl_cros_flash_write_protection_is_set(const struct device *dev)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->write_protection_is_set) {
		return -ENOTSUP;
	}

	return api->write_protection_is_set(dev);
}

/**
 * @brief Read status registers of flash.
 *
 * cmd_code must be a valid code to read the status register.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param cmd_code	instruction code to read status registers.
 * @param data	        Buffer to store the value read back
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_get_status_reg(const struct device *dev, char cmd_code,
					char *data);

static inline int z_impl_cros_flash_get_status_reg(const struct device *dev,
						   char cmd_code, char *data)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->get_status_reg) {
		return -ENOTSUP;
	}

	return api->get_status_reg(dev, cmd_code, data);
}

/**
 * @brief Write status registers of flash.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param data	        Buffer to store the value to write
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_set_status_reg(const struct device *dev, char *data);

static inline int z_impl_cros_flash_set_status_reg(const struct device *dev,
						   char *data)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->set_status_reg) {
		return -ENOTSUP;
	}

	return api->set_status_reg(dev, data);
}

/**
 * @brief Enable or disable UMA module to access the internal flash.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param enable True to lock it, False to unlock it.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_uma_lock(const struct device *dev, bool enable);

static inline int
z_impl_cros_flash_uma_lock(const struct device *dev, bool enable)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->uma_lock) {
		return -ENOTSUP;
	}

	return api->uma_lock(dev, enable);
}

/**
 * @}
 */
#include <syscalls/cros_flash.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_FLASH_H_ */
