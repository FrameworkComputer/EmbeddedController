/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief API for fingerprint sensors
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_FINGERPRINT_H_
#define ZEPHYR_INCLUDE_DRIVERS_FINGERPRINT_H_

/**
 * @brief Fingerprint sensor Interface
 * @defgroup fingerprint_interface fingerprint Interface
 * @ingroup io_interfaces
 * @{
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

/** Get fingerprint sensor width. */
#define FINGERPRINT_SENSOR_RES_X(node_id) DT_PROP(node_id, width)

/** Get fingerprint sensor height. */
#define FINGERPRINT_SENSOR_RES_Y(node_id) DT_PROP(node_id, height)

/** Get fingerprint sensor resolution (bits per pixel). */
#define FINGERPRINT_SENSOR_RES_BPP(node_id) DT_PROP(node_id, bits_per_pixel)

/** Get fingerprint sensor pixel format. */
#define FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(node_id) \
	DT_STRING_TOKEN(node_id, v4l2_pixel_format)

/** Get size of raw fingerprint image (in bytes). */
#define FINGERPRINT_SENSOR_REAL_IMAGE_SIZE(node_id) \
	((FINGERPRINT_SENSOR_RES_X(node_id) *       \
	  FINGERPRINT_SENSOR_RES_Y(node_id) *       \
	  FINGERPRINT_SENSOR_RES_BPP(node_id)) /    \
	 8)

/** Dead pixels bitmask. */
#define FINGERPRINT_ERROR_DEAD_PIXELS_MASK GENMASK(9, 0)
/** Number of dead pixels detected on the last maintenance. */
#define FINGERPRINT_ERROR_DEAD_PIXELS(errors) \
	((errors) & FINGERPRINT_ERROR_DEAD_PIXELS_MASK)
/** Unknown number of dead pixels detected on the last maintenance. */
#define FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN FINGERPRINT_ERROR_DEAD_PIXELS_MASK
/** No interrupt from the sensor. */
#define FINGERPRINT_ERROR_NO_IRQ BIT(12)
/** SPI communication error. */
#define FINGERPRINT_ERROR_SPI_COMM BIT(13)
/** Invalid sensor Hardware ID. */
#define FINGERPRINT_ERROR_BAD_HWID BIT(14)
/** Sensor initialization failed. */
#define FINGERPRINT_ERROR_INIT_FAIL BIT(15)

/** Fingerprint sensor information structure. */
struct fingerprint_info {
	/* Sensor identification */
	uint32_t vendor_id;
	uint32_t product_id;
	uint32_t model_id;
	uint32_t version;
	/* Image frame characteristics */
	uint32_t frame_size;
	uint32_t pixel_format; /* using V4L2_PIX_FMT_ */
	uint16_t width;
	uint16_t height;
	uint16_t bpp;
	uint16_t errors;
};

/** Fingerprint sensor operation mode. */
enum fingerprint_sensor_mode {
	/** The sensor is waiting for requests. */
	FINGERPRINT_SENSOR_MODE_IDLE = 0,
	/** Low power mode. */
	FINGERPRINT_SENSOR_MODE_LOW_POWER = 1,
	/** The sensor is configured to detect finger. */
	FINGERPRINT_SENSOR_MODE_DETECT = 2,
};

/**
 * Image capture mode.
 *
 * @note This enum must remain ordered, if you add new values you must ensure
 * that FINGERPRINT_CAPTURE_TYPE_MAX is still the last one.
 */
enum fingerprint_capture_type {
	/**
	 * Capture 1-3 images and choose the best quality image (produces
	 * 'frame_size' bytes).
	 */
	FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT = 0,
	/** Simple raw image capture (produces width x height x bpp bits). */
	FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE = 1,
	/** Self test pattern (e.g. checkerboard). */
	FINGERPRINT_CAPTURE_TYPE_PATTERN0 = 2,
	/** Self test pattern (e.g. inverted checkerboard). */
	FINGERPRINT_CAPTURE_TYPE_PATTERN1 = 3,
	/** Capture for quality test with fixed contrast. */
	FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST = 4,
	/** Capture for pixel reset value test. */
	FINGERPRINT_CAPTURE_TYPE_RESET_TEST = 5,
	/** End of enum. */
	FINGERPRINT_CAPTURE_TYPE_MAX,
};

/** Finger state on the sensor. */
enum fingerprint_finger_state {
	/** Finger is not present. */
	FINGERPRINT_FINGER_STATE_NONE = 0,
	/** The sensor is not fully covered with the finger. */
	FINGERPRINT_FINGER_STATE_PARTIAL = 1,
	/** Finger is present. */
	FINGERPRINT_FINGER_STATE_PRESENT = 2,
};

/** Fingerprint scan status. */
enum fingerprint_sensor_scan {
	/** Image captured. */
	FINGERPRINT_SENSOR_SCAN_GOOD = 0,
	/** Image captured but quality is too low. */
	FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY = 1,
	/** Finger removed before image was captured. */
	FINGERPRINT_SENSOR_SCAN_TOO_FAST = 2,
	/** Sensor not fully covered by finger. */
	FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE = 3,
};

/**
 * @typedef fingerprint_callback_t
 * @brief Fingerprint callback for fingerprint events
 *
 * @param dev Fingerprint sensor device
 */
typedef void (*fingerprint_callback_t)(const struct device *dev);

/**
 * @typedef fingerprint_api_init_t
 * @brief Callback API for initializing fingerprint sensor.
 *
 * @param dev Fingerprint sensor device.
 */
typedef int (*fingerprint_api_init_t)(const struct device *dev);

/**
 * @typedef fingerprint_api_deinit_t
 * @brief Callback API for deinitializing fingerprint sensor.
 *
 * @param dev Fingerprint sensor device.
 */
typedef int (*fingerprint_api_deinit_t)(const struct device *dev);

/**
 * @typedef fingerprint_api_config_t
 * @brief Callback API for configuring fingerprint sensor.
 *
 * @param dev Fingerprint sensor device.
 * @param cb Callback executed on event.
 */
typedef int (*fingerprint_api_config_t)(const struct device *dev,
					fingerprint_callback_t cb);

/**
 * @typedef fingerprint_api_get_info_t
 * @brief Callback API for getting information about fingerprint sensor.
 *
 * @param dev Fingerprint sensor device.
 * @param info Pointer to fingerprint_info structure where data will be stored.
 */
typedef int (*fingerprint_api_get_info_t)(const struct device *dev,
					  struct fingerprint_info *info);

/**
 * @typedef fingerprint_api_maintenance_t
 * @brief Callback API for maintenance operation.
 *
 * @param dev Fingerprint sensor device.
 * @param buf Buffer used during maintenance procedure.
 * @param size Size of the buffer.
 */
typedef int (*fingerprint_api_maintenance_t)(const struct device *dev,
					     uint8_t *buf, size_t size);

/**
 * @typedef fingerprint_api_set_mode_t
 * @brief Callback API for changing fingerprint sensor mode.
 *
 * @param dev Fingerprint sensor device.
 * @param mode Fingerprint sensor mode.
 */
typedef int (*fingerprint_api_set_mode_t)(const struct device *dev,
					  enum fingerprint_sensor_mode mode);

/**
 * @typedef fingerprint_api_acquire_image_t
 * @brief Callback API for acquiring fingerprint image.
 *
 * @param dev Fingerprint sensor device.
 * @param mode One of the mode from fingerprint_capture_type enum.
 * @param image Pointer to buffer where image should be stored.
 * @param size Size of the buffer.
 */
typedef int (*fingerprint_api_acquire_image_t)(const struct device *dev,
					       int mode, uint8_t *image,
					       size_t size);

/**
 * @typedef fingerprint_api_finger_status_t
 * @brief Callback API for the status of the finger on the sensor
 *
 * @param dev Fingerprint sensor device.
 */
typedef int (*fingerprint_api_finger_status_t)(const struct device *dev);

/** Driver API structure. */
__subsystem struct fingerprint_driver_api {
	fingerprint_api_init_t init;
	fingerprint_api_deinit_t deinit;
	fingerprint_api_config_t config;
	fingerprint_api_get_info_t get_info;
	fingerprint_api_maintenance_t maintenance;
	fingerprint_api_set_mode_t set_mode;
	fingerprint_api_acquire_image_t acquire_image;
	fingerprint_api_finger_status_t finger_status;
};

/**
 * @brief Initialize fingerprint sensor.
 *
 * @param dev Pointer to the device structure for the fingerprint sensor driver
 *	      instance.
 *
 * @retval 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 * @retval other negative values indicates driver specific error.
 */
__syscall int fingerprint_init(const struct device *dev);

static inline int z_impl_fingerprint_init(const struct device *dev)
{
	const struct fingerprint_driver_api *api =
		(const struct fingerprint_driver_api *)dev->api;

	if (api->init == NULL) {
		return -ENOTSUP;
	}

	return api->init(dev);
}

/**
 * @brief Deinitialize fingerprint sensor.
 *
 * @param dev Pointer to the device structure for the fingerprint sensor driver
 *	      instance.
 *
 * @retval 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 * @retval other negative values indicates driver specific error.
 */
__syscall int fingerprint_deinit(const struct device *dev);

static inline int z_impl_fingerprint_deinit(const struct device *dev)
{
	const struct fingerprint_driver_api *api =
		(const struct fingerprint_driver_api *)dev->api;

	if (api->deinit == NULL) {
		return -ENOTSUP;
	}

	return api->deinit(dev);
}

/**
 * @brief Configure fingerprint sensor.
 *
 * @param dev Pointer to the device structure for the fingerprint sensor driver
 *	      instance.
 * @param cb  Callback executed on fingerprint event.
 *
 * @retval 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int fingerprint_config(const struct device *dev,
				 fingerprint_callback_t cb);

static inline int z_impl_fingerprint_config(const struct device *dev,
					    fingerprint_callback_t cb)
{
	const struct fingerprint_driver_api *api =
		(const struct fingerprint_driver_api *)dev->api;

	if (api->config == NULL) {
		return -ENOTSUP;
	}

	return api->config(dev, cb);
}

/**
 * @brief Get information about fingerprint sensor.
 *
 * @param dev  Pointer to the device structure for the fingerprint sensor driver
 *	       instance.
 * @param info Pointer to 'fingerprint_info' structure where data will be
 *	       stored.
 *
 * @retval 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 * @retval other negative values indicates driver specific error.
 */
__syscall int fingerprint_get_info(const struct device *dev,
				   struct fingerprint_info *info);

static inline int z_impl_fingerprint_get_info(const struct device *dev,
					      struct fingerprint_info *info)
{
	const struct fingerprint_driver_api *api =
		(const struct fingerprint_driver_api *)dev->api;

	if (api->get_info == NULL) {
		return -ENOTSUP;
	}

	return api->get_info(dev, info);
}

/**
 * @brief Start fingerprint maintenance operation.
 *
 * @param dev Pointer to the device structure for the fingerprint sensor driver
 *	      instance.
 *
 * @retval 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 * @retval -EINVAL Invalid argument was passed (e.g. size of buffer).
 * @retval other negative values indicates driver specific error.
 */
__syscall int fingerprint_maintenance(const struct device *dev, uint8_t *buf,
				      size_t size);

static inline int z_impl_fingerprint_maintenance(const struct device *dev,
						 uint8_t *buf, size_t size)
{
	const struct fingerprint_driver_api *api =
		(const struct fingerprint_driver_api *)dev->api;

	if (api->maintenance == NULL) {
		return -ENOTSUP;
	}

	return api->maintenance(dev, buf, size);
}

/**
 * @brief Change fingerprint sensor mode.
 *
 * @param dev Pointer to the device structure for the fingerprint sensor driver
 *	      instance.
 * @param mode Target fingerprint sensor mode.
 *
 * @retval 0 If successful.
 * @retval -ENOTSUP Unsupported mode or api function.
 * @retval other negative values indicates driver specific error.
 */
__syscall int fingerprint_set_mode(const struct device *dev,
				   enum fingerprint_sensor_mode mode);

static inline int z_impl_fingerprint_set_mode(const struct device *dev,
					      enum fingerprint_sensor_mode mode)
{
	const struct fingerprint_driver_api *api =
		(const struct fingerprint_driver_api *)dev->api;

	if (api->set_mode == NULL) {
		return -ENOTSUP;
	}

	return api->set_mode(dev, mode);
}

/**
 * @brief Acquire image of a finger.
 *
 * @param dev Pointer to the device structure for the fingerprint sensor driver
 *	      instance.
 * @param mode One of the mode from fingerprint_capture_type enum.
 * @param image Pointer to buffer where image should be stored.
 * @param size Size of the buffer.
 *
 * @retval 0 or positive values, representing fingerprint_sensor_scan enum,
 *	   if request completed successfully.
 * @retval -ENOTSUP Not supported api function.
 * @retval -EINVAL Invalid argument was passed (e.g. size of buffer).
 * @retval other negative values indicates driver specific error.
 */
__syscall int fingerprint_acquire_image(const struct device *dev, int mode,
					uint8_t *image, size_t size);

static inline int z_impl_fingerprint_acquire_image(const struct device *dev,
						   int mode, uint8_t *image,
						   size_t size)
{
	const struct fingerprint_driver_api *api =
		(const struct fingerprint_driver_api *)dev->api;

	if (api->acquire_image == NULL) {
		return -ENOTSUP;
	}

	return api->acquire_image(dev, mode, image, size);
}

/**
 * @brief Get status of the finger on the sensor.
 *
 * @param dev Pointer to the device structure for the fingerprint sensor driver
 *	      instance.
 * @param status Pointer to variable where status should be written
 *
 * @retval 0 or positive values, representing fingerprint_finger_state enum,
 *	   if successful.
 * @retval -ENOTSUP Not supported api function.
 * @retval other negative values indicates driver specific error.
 */
__syscall int fingerprint_finger_status(const struct device *dev);

static inline int z_impl_fingerprint_finger_status(const struct device *dev)
{
	const struct fingerprint_driver_api *api =
		(const struct fingerprint_driver_api *)dev->api;

	if (api->finger_status == NULL) {
		return -ENOTSUP;
	}

	return api->finger_status(dev);
}

/**
 * @}
 */
#include <syscalls/fingerprint.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_FINGERPRINT_H_ */
