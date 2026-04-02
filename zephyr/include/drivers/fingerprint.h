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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get fingerprint sensor width for a given configuration index.
 *
 * @param idx Index of the configuration to retrieve the width from.
 * @param node_id Devicetree node identifier for the sensor.
 * @return Sensor width.
 */
#define FINGERPRINT_SENSOR_RES_X(idx, node_id) \
	DT_PROP(DT_CHILD(DT_CHILD(node_id, configs), config##idx), width)

/**
 * @brief Get fingerprint sensor height for a given configuration index.
 *
 * @param idx Index of the configuration to retrieve the height from.
 * @param node_id Devicetree node identifier for the sensor.
 * @return Sensor height.
 */
#define FINGERPRINT_SENSOR_RES_Y(idx, node_id) \
	DT_PROP(DT_CHILD(DT_CHILD(node_id, configs), config##idx), height)

/**
 * @brief Get fingerprint sensor resolution (bits per pixel) for a given
 * configuration index.
 *
 * @param idx Index of the configuration to retrieve the bits per pixel from.
 * @param node_id Devicetree node identifier for the sensor.
 * @return Sensor bits per pixel.
 */
#define FINGERPRINT_SENSOR_RES_BPP(idx, node_id)                   \
	DT_PROP(DT_CHILD(DT_CHILD(node_id, configs), config##idx), \
		bits_per_pixel)

/**
 * @brief Get fingerprint sensor capture type for a given configuration index.
 *
 * @param idx Index of the configuration to retrieve the capture type from.
 * @param node_id Devicetree node identifier for the sensor.
 * @return Sensor capture type (enum fp_capture_type).
 */
#define FINGERPRINT_SENSOR_CAPTURE_TYPE(idx, node_id)                      \
	DT_STRING_TOKEN(DT_CHILD(DT_CHILD(node_id, configs), config##idx), \
			capture_type)

/**
 * @brief Get fingerprint sensor pixel format for a given configuration index.
 *
 * @param idx Index of the configuration to retrieve the pixel format from.
 * @param node_id Devicetree node identifier for the sensor.
 * @return Sensor V4L2 pixel format token.
 */
#define FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(idx, node_id)                 \
	DT_STRING_TOKEN(DT_CHILD(DT_CHILD(node_id, configs), config##idx), \
			v4l2_pixel_format)

/**
 * @brief Get the total size of the raw fingerprint data frame in bytes for a
 * given configuration index.
 *
 * This value is read from the 'frame_size' property in the Device Tree.
 * The frame size may be larger than the actual underlying image pixel data
 * size (see FINGERPRINT_SENSOR_REAL_IMAGE_SIZE, calculated by width * height *
 * bpp) as it can include sensor-specific metadata, protocol overhead, or
 * padding.
 *
 * @param idx Index of the configuration to retrieve the frame size from.
 * @param node_id Devicetree node identifier for the sensor.
 * @return Total raw frame size in bytes.
 */
#define FINGERPRINT_SENSOR_FRAME_SIZE(idx, node_id) \
	DT_PROP(DT_CHILD(DT_CHILD(node_id, configs), config##idx), frame_size)

/**
 * @brief Get fingerprint sensor image offset for a given configuration index.
 *
 * @param idx Index of the configuration to retrieve the offset from.
 * @param node_id Devicetree node identifier for the sensor.
 * @return Image offset in bytes.
 */
#define FINGERPRINT_SENSOR_IMAGE_OFFSET(idx, node_id)              \
	DT_PROP(DT_CHILD(DT_CHILD(node_id, configs), config##idx), \
		image_data_offset_bytes)

/**
 * @brief Byte offset of the image payload within the raw sensor buffer.
 *
 * This identifies the start of pixel data for the default configuration
 * (index 0), excluding hardware headers or metadata.
 *
 * @return Number of bytes to skip to reach the first pixel.
 */
#define IMAGE_OFFSET                       \
	FINGERPRINT_SENSOR_IMAGE_OFFSET(0, \
					DT_CHOSEN(cros_fp_fingerprint_sensor))
/**
 * @brief Get the real size of the image pixel data in bytes.
 *
 * This macro calculates the **actual image size** in bytes for a specific
 * capture configuration defined in the Device Tree. It multiplies the width (X
 * resolution), height (Y resolution), and bits-per-pixel (BPP), then divides
 * the result by 8 to convert the total number of bits into bytes. This value
 * represents the image data without any protocol overhead or metadata.
 *
 * @param idx Index of the configuration to retrieve the frame size from.
 * @param node_id Devicetree node identifier for the sensor.
 * @return The raw image size in bytes.
 */
#define FINGERPRINT_SENSOR_REAL_IMAGE_SIZE(idx, node_id) \
	((FINGERPRINT_SENSOR_RES_X(idx, node_id) *       \
	  FINGERPRINT_SENSOR_RES_Y(idx, node_id) *       \
	  FINGERPRINT_SENSOR_RES_BPP(idx, node_id)) /    \
	 8)

/**
 * @brief Get the maximum frame size supported by a specific fingerprint sensor.
 *
 * This macro uses the LISTIFY and MAX_FROM_LIST utilities to find the
 * largest image frame size across all supported image capture types for the
 * fingerprint sensor identified by @p node_id. The result is determined at
 * compile time.
 *
 * @param node_id Devicetree node identifier for the sensor.
 * @return The maximum supported frame size in bytes.
 */
#define MAX_FRAME_SIZE(node_id)                        \
	MAX_FROM_LIST(LISTIFY(NUM_IMAGE_CAPTURE_TYPES, \
			      FINGERPRINT_SENSOR_FRAME_SIZE, (, ), node_id))

/**
 * @brief Get the number of capture configurations defined for a fingerprint
 * sensor node.
 *
 * This macro retrieves the number of capture configurations defined for a
 * fingerprint sensor node from the specified Device Tree node. It assumes that
 * all related array properties (e.g., height, bits_per_pixel, frame_size,
 * capture_type) for the different sensor configurations have the same length as
 * the 'width' array.
 *
 * @param node_id Devicetree node identifier for the sensor.
 * @return The number of distinct capture configurations available for the
 * sensor.
 */
#define FINGERPRINT_SENSOR_NUM_CONFIGS(node_id) \
	DT_CHILD_NUM(DT_CHILD(node_id, configs))

/**
 * @brief Get the number of image capture configurations for the system's
 * primary fingerprint sensor.
 *
 * This macro utilizes FINGERPRINT_SENSOR_NUM_CONFIGS to determine the
 * number of different image capture setups (e.g., resolutions, formats)
 * available for the fingerprint sensor instance designated by the
 * 'cros_fp_fingerprint_sensor' chosen node in the Device Tree. This
 * effectively counts the elements in the configuration arrays (like 'width',
 * 'height', etc.) for the selected sensor.
 *
 * @return The number of image capture types supported by the chosen
 * fingerprint sensor.
 */
#define NUM_IMAGE_CAPTURE_TYPES \
	FINGERPRINT_SENSOR_NUM_CONFIGS(DT_CHOSEN(cros_fp_fingerprint_sensor))

/** Dead pixels bitmask. */
#define FINGERPRINT_ERROR_DEAD_PIXELS_MASK GENMASK(9, 0)
/* Maximum number of dead pixels */
#define FINGERPRINT_ERROR_DEAD_PIXELS_MAX \
	(FINGERPRINT_ERROR_DEAD_PIXELS_MASK - 1)
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

/**
 * @brief Fingerprint sensor identification.
 *
 * This structure holds information that is constant after sensor
 * initialization, except for the errors field which can change at runtime.
 */
struct fingerprint_sensor_info {
	/** @brief Sensor vendor ID. */
	uint32_t vendor_id;
	/** @brief Sensor product ID. */
	uint32_t product_id;
	/** @brief Sensor model ID. */
	uint32_t model_id;
	/** @brief Sensor hardware/firmware version. */
	uint32_t version;
	/**
	 * @brief Number of image capture types supported by the sensor.
	 * @see enum fingerprint_capture_type
	 */
	uint16_t num_capture_types;
	/** @brief Current sensor error flags (bitmask of FINGERPRINT_ERROR_*).
	 */
	uint16_t errors;
};

/**
 * @brief Parameters for a single fingerprint image frame.
 *
 * This structure describes the properties of a captured image frame.
 */
struct fingerprint_image_frame_params {
	/** @brief Total size of the frame data in bytes. */
	uint32_t frame_size;
	/** @brief Image offset in bytes from the start of the sensor buffer. */
	uint32_t image_data_offset_bytes;
	/**
	 * @brief Pixel format of the image.
	 * It is recommended to use V4L2_PIX_FMT_* definitions where applicable.
	 */
	uint32_t pixel_format;
	/** @brief Image width in pixels. */
	uint16_t width;
	/** @brief Image height in pixels. */
	uint16_t height;
	/** @brief Bits per pixel for the image. */
	uint16_t bpp;
	/**
	 * @brief Type of image capture.
	 * @see enum fingerprint_capture_type
	 */
	uint8_t fp_capture_type;
	/** @brief Reserved for padding and alignment. Should be zero. */
	uint8_t reserved;
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
 * Image capture type.
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
	/** Capture for check defect pixel test */
	FINGERPRINT_CAPTURE_TYPE_DEFECT_PXL_TEST = 1,
	/** Capture for check abnormal pixel test */
	FINGERPRINT_CAPTURE_TYPE_ABNORMAL_TEST = 2,
	/** Capture for check noise test */
	FINGERPRINT_CAPTURE_TYPE_NOISE_TEST = 3,
	/** Simple raw image capture (produces width x height x bpp bits). */
	FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE = 4,
	/** Self test pattern (e.g. checkerboard). */
	FINGERPRINT_CAPTURE_TYPE_PATTERN0 = 8,
	/** Self test pattern (e.g. inverted checkerboard). */
	FINGERPRINT_CAPTURE_TYPE_PATTERN1 = 12,
	/** Capture for quality test with fixed contrast. */
	FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST = 16,
	/** Capture for pixel reset value test. */
	FINGERPRINT_CAPTURE_TYPE_RESET_TEST = 20,
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
 * @param sensor_info Pointer to a struct where the sensor's static information
 * will be stored.
 * @param image_frame_params_array Pointer to a struct where the sensor's
 * image frame parameters (e.g., width, height, format) will be stored.
 * @param[in,out] num_params On input, contains the number of elements allocated
 * in image_frame_params_array. On output, contains the actual number of
 * elements written to image_frame_params_array.
 */
typedef int (*fingerprint_api_get_info_t)(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params image_frame_params_array[],
	uint8_t *num_params);

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
 * @param capture_type One of the capture types from fingerprint_capture_type
 *                     enum.
 * @param image Pointer to buffer where image should be stored.
 * @param size Size of the buffer.
 */
typedef int (*fingerprint_api_acquire_image_t)(
	const struct device *dev, enum fingerprint_capture_type capture_type,
	uint8_t *image, size_t size);

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
	if (DEVICE_API_GET(fingerprint, dev)->init == NULL) {
		return -ENOTSUP;
	}

	return DEVICE_API_GET(fingerprint, dev)->init(dev);
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
	if (DEVICE_API_GET(fingerprint, dev)->deinit == NULL) {
		return -ENOTSUP;
	}

	return DEVICE_API_GET(fingerprint, dev)->deinit(dev);
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
	if (DEVICE_API_GET(fingerprint, dev)->config == NULL) {
		return -ENOTSUP;
	}

	return DEVICE_API_GET(fingerprint, dev)->config(dev, cb);
}

/**
 * @brief Get information about fingerprint sensor.
 *
 * @param dev  Pointer to the device structure for the fingerprint sensor driver
 *	       instance.
 * @param sensor_info Pointer to 'fingerprint_sensor_info' structure where the
 * sensor's static information will be stored.
 * @param image_frame_params Pointer to 'fingerprint_image_frame_params'
 * structure where the sensor's image frame parameters (e.g., width, height,
 * format) will be stored.
 * @param[in,out] num_params On input, contains the number of elements allocated
 * in image_frame_params_array. On output, contains the actual number of
 * elements written to image_frame_params_array.
 * @retval 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 * @retval other negative values indicates driver specific error.
 */
__syscall int fingerprint_get_info(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params *image_frame_params_array,
	uint8_t *num_params);

static inline int z_impl_fingerprint_get_info(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params image_frame_params_array[],
	uint8_t *num_params)
{
	if (DEVICE_API_GET(fingerprint, dev)->get_info == NULL) {
		return -ENOTSUP;
	}

	return DEVICE_API_GET(fingerprint, dev)
		->get_info(dev, sensor_info, image_frame_params_array,
			   num_params);
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
	if (DEVICE_API_GET(fingerprint, dev)->maintenance == NULL) {
		return -ENOTSUP;
	}

	return DEVICE_API_GET(fingerprint, dev)->maintenance(dev, buf, size);
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
	if (DEVICE_API_GET(fingerprint, dev)->set_mode == NULL) {
		return -ENOTSUP;
	}

	return DEVICE_API_GET(fingerprint, dev)->set_mode(dev, mode);
}

/**
 * @brief Acquire image of a finger.
 *
 * @param dev Pointer to the device structure for the fingerprint sensor driver
 *	      instance.
 * @param capture_type One of the capture types from fingerprint_capture_type
 *                     enum.
 * @param image Pointer to buffer where image should be stored.
 * @param size Size of the buffer.
 *
 * @retval 0 or positive values, representing fingerprint_sensor_scan enum,
 *	   if request completed successfully.
 * @retval -ENOTSUP Not supported api function.
 * @retval -EINVAL Invalid argument was passed (e.g. size of buffer).
 * @retval other negative values indicates driver specific error.
 */
__syscall int
fingerprint_acquire_image(const struct device *dev,
			  enum fingerprint_capture_type capture_type,
			  uint8_t *image, size_t size);

static inline int
z_impl_fingerprint_acquire_image(const struct device *dev,
				 enum fingerprint_capture_type capture_type,
				 uint8_t *image, size_t size)
{
	if (DEVICE_API_GET(fingerprint, dev)->acquire_image == NULL) {
		return -ENOTSUP;
	}

	return DEVICE_API_GET(fingerprint, dev)
		->acquire_image(dev, capture_type, image, size);
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
	if (DEVICE_API_GET(fingerprint, dev)->finger_status == NULL) {
		return -ENOTSUP;
	}

	return DEVICE_API_GET(fingerprint, dev)->finger_status(dev);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */
#include <zephyr/syscalls/fingerprint.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_FINGERPRINT_H_ */
