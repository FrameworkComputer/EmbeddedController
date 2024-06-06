/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Chrome OS-specific API for real-time clock (RTC).
 * This exists only support the interface expected by the Chrome OS EC. It
 * provides raw access to RTC module.
 *
 * This API and any drivers should be removed once we can safely move to using
 * the Zephyr rtc API.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_RTC_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_RTC_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>

/**
 * @brief CROS Real-Time Clock (RTC) Driver APIs
 * @defgroup cros_rtc_interface CROS RTC Driver APIs
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief RTC alarm callback
 *
 * @param dev Pointer to the device structure for the RTC driver instance.
 */
typedef void (*cros_rtc_alarm_callback_t)(const struct device *dev);

/**
 * @cond INTERNAL_HIDDEN
 *
 * cros real-time clock driver API definition and system call entry points
 *
 * (Internal use only.)
 */
typedef int (*cros_rtc_api_configure)(const struct device *dev,
				      cros_rtc_alarm_callback_t callback);

typedef int (*cros_rtc_api_get_value)(const struct device *dev,
				      uint32_t *value);

typedef int (*cros_rtc_api_set_value)(const struct device *dev, uint32_t value);

typedef int (*cros_rtc_api_get_alarm)(const struct device *dev,
				      uint32_t *seconds,
				      uint32_t *microseconds);

typedef int (*cros_rtc_api_set_alarm)(const struct device *dev,
				      uint32_t seconds, uint32_t microseconds);

typedef int (*cros_rtc_api_reset_alarm)(const struct device *dev);

__subsystem struct cros_rtc_driver_api {
	cros_rtc_api_configure configure;
	cros_rtc_api_get_value get_value;
	cros_rtc_api_set_value set_value;
	cros_rtc_api_get_alarm get_alarm;
	cros_rtc_api_set_alarm set_alarm;
	cros_rtc_api_reset_alarm reset_alarm;
};

/**
 * @endcond
 */

/**
 * @brief Configure real-time clock callback func.
 *
 * @param dev Pointer to the device structure for the RTC driver instance.
 * @param callback Callback func when RTC alarm issued.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 * @retval -EINVAL Not valid callback func.
 */
__syscall int cros_rtc_configure(const struct device *dev,
				 cros_rtc_alarm_callback_t callback);
static inline int z_impl_cros_rtc_configure(const struct device *dev,
					    cros_rtc_alarm_callback_t callback)
{
	const struct cros_rtc_driver_api *api =
		(const struct cros_rtc_driver_api *)dev->api;

	if (!api->configure) {
		return -ENOTSUP;
	}

	return api->configure(dev, callback);
}

/**
 * @brief Get the current real-time clock value.
 *
 * @param dev Pointer to the device structure for the RTC driver instance.
 * @param value Pointer to the number of current real-time clock value.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_rtc_get_value(const struct device *dev, uint32_t *value);
static inline int z_impl_cros_rtc_get_value(const struct device *dev,
					    uint32_t *value)
{
	const struct cros_rtc_driver_api *api =
		(const struct cros_rtc_driver_api *)dev->api;

	if (!api->get_value) {
		return -ENOTSUP;
	}

	return api->get_value(dev, value);
}

/**
 * @brief Set a desired value to real-time clock.
 *
 * @param dev Pointer to the device structure for the RTC driver instance.
 * @param value Number of desired real-time clock value.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_rtc_set_value(const struct device *dev, uint32_t value);
static inline int z_impl_cros_rtc_set_value(const struct device *dev,
					    uint32_t value)
{
	const struct cros_rtc_driver_api *api =
		(const struct cros_rtc_driver_api *)dev->api;

	if (!api->set_value) {
		return -ENOTSUP;
	}

	return api->set_value(dev, value);
}

/**
 * @brief Get a given time when an RTC alarm interrupt issued.
 *
 * @param dev Pointer to the device structure for the RTC driver instance.
 * @param seconds Pointer to number of seconds before RTC alarm issued.
 * @param microseconds Pointer to number of micro-secs before RTC alarm issued.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_rtc_get_alarm(const struct device *dev, uint32_t *seconds,
				 uint32_t *microseconds);

static inline int z_impl_cros_rtc_get_alarm(const struct device *dev,
					    uint32_t *seconds,
					    uint32_t *microseconds)
{
	const struct cros_rtc_driver_api *api =
		(const struct cros_rtc_driver_api *)dev->api;

	if (!api->get_alarm) {
		return 0;
	}

	return api->get_alarm(dev, seconds, microseconds);
}

/**
 * @brief Set up an RTC alarm interrupt at a given time from now
 *
 * @param dev Pointer to the device structure for the RTC driver instance.
 * @param seconds Number of seconds before RTC alarm issued.
 * @param microseconds Number of microseconds before alarm RTC issued.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_rtc_set_alarm(const struct device *dev, uint32_t seconds,
				 uint32_t microseconds);

static inline int z_impl_cros_rtc_set_alarm(const struct device *dev,
					    uint32_t seconds,
					    uint32_t microseconds)
{
	const struct cros_rtc_driver_api *api =
		(const struct cros_rtc_driver_api *)dev->api;

	if (!api->set_alarm) {
		return 0;
	}

	return api->set_alarm(dev, seconds, microseconds);
}

/**
 * @brief Disable and clear the RTC alarm interrupt.
 *
 * @param dev Pointer to the device structure for the RTC driver instance.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_rtc_reset_alarm(const struct device *dev);

static inline int z_impl_cros_rtc_reset_alarm(const struct device *dev)
{
	const struct cros_rtc_driver_api *api =
		(const struct cros_rtc_driver_api *)dev->api;

	if (!api->reset_alarm) {
		return -ENOTSUP;
	}

	return api->reset_alarm(dev);
}

/**
 * @}
 */
#include <zephyr/syscalls/cros_rtc.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_RTC_H_ */
