/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT egis_egis660

#include "fingerprint_egis660.h"
#include "fingerprint_egis660_pal.h"
#include "fingerprint_egis660_private.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>

LOG_MODULE_REGISTER(cros_fingerprint, LOG_LEVEL_INF);

int convert_fp_capture_type_to_egis_capture_type(
	enum fingerprint_capture_type mode)
{
	switch (mode) {
	case FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT:
		return EGIS_CAPTURE_VENDOR_FORMAT;
	case FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE:
		return EGIS_CAPTURE_SIMPLE_IMAGE;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN0:
		return EGIS_CAPTURE_PATTERN0;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN1:
		return EGIS_CAPTURE_PATTERN1;
	case FINGERPRINT_CAPTURE_TYPE_RESET_TEST:
		return EGIS_CAPTURE_RESET_TEST;
	default:
		return -EINVAL;
	}
}

void fp_sensor_lock(const struct device *dev)
{
	struct egis660_data *data = dev->data;

	/* Lock SPI access only if we are not already the owner. */
	if (!((k_sem_count_get(&data->sensor_lock) == 0) &&
	      (data->sensor_owner == k_current_get()))) {
		k_sem_take(&data->sensor_lock, K_FOREVER);
		data->sensor_owner = k_current_get();
	}
}

void fp_sensor_unlock(const struct device *dev)
{
	struct egis660_data *data = dev->data;

	/* Clear the owner and return the access. */
	data->sensor_owner = NULL;
	k_sem_give(&data->sensor_lock);
}

static inline int egis660_enable_irq(const struct device *dev)
{
	const struct egis660_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					     GPIO_INT_EDGE_TO_ACTIVE);
	if (rc < 0) {
		LOG_ERR("Can't enable interrupt: %d", rc);
	}

	return rc;
}

static inline int egis660_disable_irq(const struct device *dev)
{
	const struct egis660_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt, GPIO_INT_DISABLE);
	if (rc < 0) {
		LOG_ERR("Can't disable interrupt: %d", rc);
	}

	return rc;
}

static int egis660_set_mode(const struct device *dev,
			    enum fingerprint_sensor_mode mode)
{
	int rc = 0, deepsleep_rc;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		if (IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
			fp_sensor_configure_detect();
			rc = egis660_enable_irq(dev);
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		if (IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
			rc = egis660_disable_irq(dev);
			if (rc != 0) {
				LOG_ERR("Failed to disable IRQ: %d", rc);
			}

			deepsleep_rc = fp_sensor_deepsleep();
			if (deepsleep_rc != 0) {
				LOG_ERR("Failed to enter deep sleep: %d",
					deepsleep_rc);
				if (rc == 0) {
					rc = deepsleep_rc;
				}
			}
		} else {
			rc = -ENOTSUP;
		}

		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		rc = egis660_disable_irq(dev);
		break;

	default:
		rc = -ENOTSUP;
	}

	return rc;
}

static int egis660_init(const struct device *dev)
{
	struct egis660_data *data = dev->data;
	uint16_t id = 0;
	int rc = 0;

	if (IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
		/* Print the binary libfp.a library version. */
		LOG_INF("EGIS libfp.a %s", fp_sensor_get_version());

		/* Print the BEP version and build time of the library. */
		LOG_INF("Build information - %s", fp_sensor_get_build_info());
	}

	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;

	if (IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
		rc = fp_sensor_get_hwid(&id);
	}

	if (rc) {
		LOG_ERR("Failed to get EGIS HWID: %d", rc);
		data->errors |= FINGERPRINT_ERROR_SPI_COMM;
		return rc;
	}

	LOG_INF("EGIS660 id 0x%04x", id);

	if (IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
		rc = fp_sensor_open();
		if (rc) {
			LOG_ERR("fp_sensor_open() failed, result %d", rc);
			data->errors |= FINGERPRINT_ERROR_INIT_FAIL;
			return rc;
		}
	}

	egis660_set_mode(dev, FINGERPRINT_SENSOR_MODE_LOW_POWER);

	return 0;
}

static int egis660_deinit(const struct device *dev)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
		return 0;
	}

	rc = fp_sensor_close();
	if (rc < 0) {
		LOG_ERR("fp_sensor_close() failed, result %d", rc);
		return rc;
	}

	return 0;
}

static int egis660_get_info(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params image_frame_params_array[],
	uint8_t *num_params)
{
	const struct egis660_cfg *cfg = dev->config;
	struct egis660_data *data = dev->data;
	uint16_t id = 0;
	int rc = 0;

	if ((sensor_info == NULL) || (num_params == NULL) ||
	    (image_frame_params_array == NULL)) {
		return -EINVAL;
	}

	uint8_t capacity = *num_params;
	uint8_t num_defined_configs = cfg->sensor_info.num_capture_types;

	if (capacity < num_defined_configs) {
		return -EINVAL;
	}

	BUILD_ASSERT(sizeof(cfg->sensor_info) == sizeof(*sensor_info),
		     "struct fingerprint_sensor_info size mismatch");

	memcpy(sensor_info, &cfg->sensor_info,
	       sizeof(struct fingerprint_sensor_info));

	memcpy(image_frame_params_array, cfg->sensor_image_configs,
	       num_defined_configs *
		       sizeof(struct fingerprint_image_frame_params));

	*num_params = num_defined_configs;

	if (IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
		rc = fp_sensor_get_hwid(&id);
	}

	if (rc) {
		LOG_ERR("Failed to get EGIS HWID: %d", rc);
		return rc;
	}

	sensor_info->model_id = id;
	sensor_info->errors = data->errors;

	return 0;
}

static int egis660_config(const struct device *dev, fingerprint_callback_t cb)
{
	struct egis660_data *data = dev->data;

	data->callback = cb;

	return 0;
}

static int egis660_maintenance(const struct device *dev, uint8_t *buf,
			       size_t size)
{
	struct egis660_data *data = dev->data;
	struct egis660_fp_sensor_info sensor_info;
	uint64_t start;
	int rc = 0;

	if (!IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	if (size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE) {
		return -EINVAL;
	}

	start = k_uptime_get();

	rc = fp_sensor_maintenance(buf, &sensor_info);
	LOG_INF("Maintenance took %lld ms", k_uptime_delta(&start));

	if (rc != 0) {
		/*
		 * Failure can occur if any of the fingerprint detection zones
		 * are covered (i.e., finger is on sensor).
		 */
		LOG_WRN("Failed to run maintenance: %d", rc);
		return -EFAULT;
	}

	data->errors &= ~FINGERPRINT_ERROR_DEAD_PIXELS_MASK;
	data->errors |= FINGERPRINT_ERROR_DEAD_PIXELS(
		MIN(sensor_info.num_defective_pixels,
		    FINGERPRINT_ERROR_DEAD_PIXELS_MAX));
	LOG_INF("num_defective_pixels: %d", sensor_info.num_defective_pixels);

	return 0;
}

BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_GOOD == EGIS_SENSOR_GOOD_IMAGE_QUALITY);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY ==
	     EGIS_SENSOR_LOW_IMAGE_QUALITY);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_TOO_FAST == EGIS_SENSOR_TOO_FAST);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE ==
	     EGIS_SENSOR_LOW_COVERAGE);

static int egis660_acquire_image(const struct device *dev,
				 enum fingerprint_capture_type capture_type,
				 uint8_t *image_buf, size_t image_buf_size)
{
	int rc;

	if (image_buf_size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE)
		return -EINVAL;

	rc = convert_fp_capture_type_to_egis_capture_type(capture_type);

	if (rc < 0) {
		LOG_ERR("Unsupported capture_type %d provided", capture_type);
		return rc;
	}

	if (!IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rc = fp_sensor_acquire_image_with_mode(image_buf, rc);
	if (rc < 0) {
		LOG_ERR("Failed to acquire image with capture_type %d: %d",
			capture_type, rc);
		return rc;
	}

	/*
	 * Finger status codes returned by fp_sensor_acquire_image() are
	 * synchronized with FINGERPRINT_SENSOR_* defines.
	 */
	return rc;
}

BUILD_ASSERT(FINGERPRINT_FINGER_STATE_NONE == EGIS_FINGER_NONE);
BUILD_ASSERT(FINGERPRINT_FINGER_STATE_PARTIAL == EGIS_FINGER_PARTIAL);
BUILD_ASSERT(FINGERPRINT_FINGER_STATE_PRESENT == EGIS_FINGER_PRESENT);

static int egis660_finger_status(const struct device *dev)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_EGIS660_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rc = fp_sensor_finger_status();
	if (rc < 0) {
		LOG_ERR("Failed to get finger status: %d", rc);
		return rc;
	}

	/*
	 * Finger status codes returned by fp_sensor_finger_status() are
	 * synchronized with fingerprint_finger_state enum.
	 */
	return rc;
}

static DEVICE_API(fingerprint, cros_fp_egis660_driver_api) = {
	.init = egis660_init,
	.deinit = egis660_deinit,
	.config = egis660_config,
	.get_info = egis660_get_info,
	.maintenance = egis660_maintenance,
	.set_mode = egis660_set_mode,
	.acquire_image = egis660_acquire_image,
	.finger_status = egis660_finger_status,
};

static void egis660_irq(const struct device *dev, struct gpio_callback *cb,
			uint32_t pins)
{
	struct egis660_data *data =
		CONTAINER_OF(cb, struct egis660_data, irq_cb);

	egis660_disable_irq(data->dev);

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

static int egis660_init_driver(const struct device *dev)
{
	const struct egis660_cfg *cfg = dev->config;
	struct egis660_data *data = dev->data;
	int ret;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->spi.bus);
		return -EINVAL;
	}

	if (!gpio_is_ready_dt(&cfg->reset_pin)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->reset_pin.port);
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->reset_pin, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Can't configure sensor reset pin");
		return ret;
	}

	if (!gpio_is_ready_dt(&cfg->interrupt)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->interrupt.port);
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->interrupt, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Can't configure interrupt pin");
		return ret;
	}

	k_sem_init(&data->sensor_lock, 1, 1);

	data->dev = dev;
	gpio_init_callback(&data->irq_cb, egis660_irq, BIT(cfg->interrupt.pin));
	gpio_add_callback_dt(&cfg->interrupt, &data->irq_cb);

	return 0;
}

#define EGIS660_SENSOR_INFO(inst)                                          \
	{                                                                  \
		.vendor_id = FOURCC('E', 'G', 'I', 'S'),                   \
		.product_id = 9,                                           \
		.model_id = 1,                                             \
		.version = 1,                                              \
		.num_capture_types =                                       \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)), \
	}

#define EGIS660_IMAGE_PARAM_INITIALIZER(idx, inst)                             \
	{                                                                      \
		.frame_size =                                                  \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)), \
		.image_data_offset_bytes = FINGERPRINT_SENSOR_IMAGE_OFFSET(    \
			idx, DT_DRV_INST(inst)),                               \
		.pixel_format = FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(          \
			idx, DT_DRV_INST(inst)),                               \
		.width = FINGERPRINT_SENSOR_RES_X(idx, DT_DRV_INST(inst)),     \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, DT_DRV_INST(inst)),    \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, DT_DRV_INST(inst)),     \
		.fp_capture_type = FINGERPRINT_SENSOR_CAPTURE_TYPE(            \
			idx, DT_DRV_INST(inst)),                               \
		.reserved = 0,                                                 \
	}

#define EGIS660_BUILD_ASSERT_IMAGE_SIZE(idx, inst)                             \
	BUILD_ASSERT(                                                          \
		CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE >=                        \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)), \
		"FP image buffer size smaller than raw image size at index " #idx);

#define EGIS660_ASSERT_FRAME_SIZE_CONSISTENT(idx, inst)                        \
	BUILD_ASSERT((FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)) == \
		      FINGERPRINT_SENSOR_FRAME_SIZE(0, DT_DRV_INST(inst))),    \
		     "EGIS660: frame_size of config " #idx                     \
		     " does not match config 0");

#define EGIS660_DEFINE(inst)                                                         \
	static struct egis660_data egis660_data_##inst;                              \
	static const struct egis660_cfg egis660_cfg_##inst = {                       \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER |               \
							  SPI_WORD_SET(8) |          \
							  SPI_HOLD_ON_CS),           \
		.interrupt = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                 \
		.reset_pin = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),               \
		.sensor_info = EGIS660_SENSOR_INFO(inst),                            \
		.sensor_image_configs = { LISTIFY(                                   \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),           \
			EGIS660_IMAGE_PARAM_INITIALIZER, (, ), inst) },              \
	};                                                                           \
	LISTIFY(FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),                   \
		EGIS660_BUILD_ASSERT_IMAGE_SIZE, (;), inst)                          \
	LISTIFY(FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),                   \
		EGIS660_ASSERT_FRAME_SIZE_CONSISTENT, (;), inst)                     \
	BUILD_ASSERT(                                                                \
		FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)) <=                 \
			NUM_IMAGE_CAPTURE_TYPES,                                     \
		"EGIS660: Number of image configs exceeds NUM_IMAGE_CAPTURE_TYPES"); \
	DEVICE_DT_INST_DEFINE(inst, egis660_init_driver, NULL,                       \
			      &egis660_data_##inst, &egis660_cfg_##inst,             \
			      POST_KERNEL,                                           \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,               \
			      &cros_fp_egis660_driver_api)

DT_INST_FOREACH_STATUS_OKAY(EGIS660_DEFINE);
