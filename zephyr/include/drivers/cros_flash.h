/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
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

#include <zephyr/device.h>
#include <zephyr/kernel.h>

/* Size to reload the watchdog timer to prevent any reset. */
#define FLASH_WATCHDOG_RELOAD_SIZE 0x10000

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

typedef int (*cros_flash_api_physical_write)(const struct device *dev,
					     int offset, int size,
					     const char *data);

typedef int (*cros_flash_api_physical_erase)(const struct device *dev,
					     int offset, int size);

typedef int (*cros_flash_api_physical_get_protect)(const struct device *dev,
						   int bank);

typedef uint32_t (*cros_flash_api_physical_get_protect_flags)(
	const struct device *dev);

typedef int (*cros_flash_api_physical_protect_at_boot)(const struct device *dev,
						       uint32_t new_flags);

typedef int (*cros_flash_api_physical_protect_now)(const struct device *dev,
						   int all);

typedef int (*cros_flash_api_physical_get_jedec_id)(const struct device *dev,
						    uint8_t *manufacturer,
						    uint16_t *device);

typedef int (*cros_flash_api_physical_get_status)(const struct device *dev,
						  uint8_t *sr1, uint8_t *sr2);

__subsystem struct cros_flash_driver_api {
	cros_flash_api_init init;
	cros_flash_api_physical_write physical_write;
	cros_flash_api_physical_erase physical_erase;
	cros_flash_api_physical_get_protect physical_get_protect;
	cros_flash_api_physical_get_protect_flags physical_get_protect_flags;
	cros_flash_api_physical_protect_at_boot physical_protect_at_boot;
	cros_flash_api_physical_protect_now physical_protect_now;
	cros_flash_api_physical_get_jedec_id physical_get_jedec_id;
	cros_flash_api_physical_get_status physical_get_status;
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
 * @brief Read physical write protect setting for a flash bank.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param bank	Bank index to check.
 *
 * @return non-zero if bank is protected until reboot.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_physical_get_protect(const struct device *dev,
					      int bank);

static inline int
z_impl_cros_flash_physical_get_protect(const struct device *dev, int bank)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_get_protect) {
		return -ENOTSUP;
	}

	return api->physical_get_protect(dev, bank);
}

/* clang-format off */
/**
 * @brief Return flash protect state flags from the physical layer.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 *
 * @retval -ENOTSUP Not supported api function.
 */
__syscall
uint32_t cros_flash_physical_get_protect_flags(const struct device *dev);
/* clang-format on */

static inline uint32_t
z_impl_cros_flash_physical_get_protect_flags(const struct device *dev)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_get_protect_flags) {
		return -ENOTSUP;
	}

	return api->physical_get_protect_flags(dev);
}

/**
 * @brief Enable/disable protecting firmware/pstate at boot.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param new_flags	to protect (only EC_FLASH_PROTECT_*_AT_BOOT are
 * taken care of)
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_physical_protect_at_boot(const struct device *dev,
						  uint32_t new_flags);

static inline int
z_impl_cros_flash_physical_protect_at_boot(const struct device *dev,
					   uint32_t new_flags)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_protect_at_boot) {
		return -ENOTSUP;
	}

	return api->physical_protect_at_boot(dev, new_flags);
}

/**
 * @brief Protect now physical flash.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param all	Protect all (=1) or just read-only and pstate (=0).
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_physical_protect_now(const struct device *dev,
					      int all);

static inline int
z_impl_cros_flash_physical_protect_now(const struct device *dev, int all)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_protect_now) {
		return -ENOTSUP;
	}

	return api->physical_protect_now(dev, all);
}

/**
 * @brief Get JEDEC manufacturer and device identifiers.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param manufacturer Pointer to data where manufacturer id will be written.
 * @param device Pointer to data where device id will be written.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_physical_get_jedec_id(const struct device *dev,
					       uint8_t *manufacturer,
					       uint16_t *device);

static inline int
z_impl_cros_flash_physical_get_jedec_id(const struct device *dev,
					uint8_t *manufacturer, uint16_t *device)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_get_jedec_id)
		return -ENOTSUP;

	return api->physical_get_jedec_id(dev, manufacturer, device);
}

/**
 * @brief Get status registers.
 *
 * @param dev Pointer to the device structure for the flash driver instance.
 * @param sr1 Pointer to data where status1 register will be written.
 * @param sr2 Pointer to data where status2 register will be written.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_flash_physical_get_status(const struct device *dev,
					     uint8_t *sr1, uint8_t *sr2);

static inline int
z_impl_cros_flash_physical_get_status(const struct device *dev, uint8_t *sr1,
				      uint8_t *sr2)
{
	const struct cros_flash_driver_api *api =
		(const struct cros_flash_driver_api *)dev->api;

	if (!api->physical_get_status)
		return -ENOTSUP;

	return api->physical_get_status(dev, sr1, sr2);
}

/**
 * @}
 */
#include <zephyr/syscalls/cros_flash.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_FLASH_H_ */
