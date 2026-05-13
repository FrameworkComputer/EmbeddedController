// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define DT_DRV_COMPAT egis_egis630

#include "fingerprint_egis630.h"
#include "fingerprint_egis630_pal.h"
#include "fingerprint_egis630_private.h"

#include <assert.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>

LOG_MODULE_REGISTER(cros_fingerprint, LOG_LEVEL_INF);

IF_DISABLED(CONFIG_ZTEST, (static))
egis_capture_mode_t convert_fp_capture_type_to_egis_capture_type(
	enum fingerprint_capture_type capture_type)
{
	switch (capture_type) {
	case FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT:
		return EGIS_CAPTURE_IMAGE_COLLECTION;
	case FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE:
		return EGIS_CAPTURE_NORMAL_FORMAT;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN0:
		return EGIS_CAPTURE_BLACK_PXL_TEST;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN1:
		return EGIS_CAPTURE_WHITE_PXL_TEST;
	case FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST:
		return EGIS_CAPTURE_RV_INT_TEST;
	case FINGERPRINT_CAPTURE_TYPE_DEFECT_PXL_TEST:
		return EGIS_CAPTURE_DEFECT_PXL_TEST;
	case FINGERPRINT_CAPTURE_TYPE_ABNORMAL_TEST:
		return EGIS_CAPTURE_ABNORMAL_TEST;
	case FINGERPRINT_CAPTURE_TYPE_NOISE_TEST:
		return EGIS_CAPTURE_NOISE_TEST;
	/*  Egis does not support the reset test. */
	case FINGERPRINT_CAPTURE_TYPE_RESET_TEST:
	default:
		return EGIS_CAPTURE_TYPE_INVALID;
	}
}

static inline int egis630_enable_irq(const struct device *dev)
{
	const struct egis630_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					     GPIO_INT_EDGE_TO_INACTIVE);
	if (rc < 0) {
		LOG_ERR("Can't enable interrupt: %d", rc);
	}

	return rc;
}

static inline int egis630_disable_irq(const struct device *dev)
{
	const struct egis630_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt, GPIO_INT_DISABLE);
	if (rc < 0) {
		LOG_ERR("Can't disable interrupt: %d", rc);
	}

	return rc;
}

/* Minimum reset duration */
#define FP_SENSOR_RESET_DURATION_MS (20)

void egis_fp_reset_sensor(const struct egis630_cfg *cfg)
{
	if (cfg == NULL) {
		return;
	}

	int ret = gpio_pin_set_dt(&cfg->reset_pin, 1);
	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
		return;
	}
	k_msleep(FP_SENSOR_RESET_DURATION_MS);
	ret = gpio_pin_set_dt(&cfg->reset_pin, 0);
	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
		return;
	}
	k_msleep(FP_SENSOR_RESET_DURATION_MS);
	return;
}

IF_DISABLED(CONFIG_ZTEST, (static))
int convert_egis_get_image_error_code(egis_api_return_t code)
{
	switch (code) {
	case EGIS_API_IMAGE_QUALITY_GOOD:
		return FINGERPRINT_SENSOR_SCAN_GOOD;
	case EGIS_API_IMAGE_QUALITY_BAD:
	case EGIS_API_IMAGE_QUALITY_WATER:
		return FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY;
	case EGIS_API_IMAGE_EMPTY:
		return FINGERPRINT_SENSOR_SCAN_TOO_FAST;
	case EGIS_API_IMAGE_QUALITY_PARTIAL:
		return FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE;
	case EGIS_API_ERROR:
		return -EINVAL;
	default:
		assert(code < 0);
		return code;
	}
}

IF_DISABLED(CONFIG_ZTEST, (static))
uint16_t convert_egis_sensor_init_error_code(egis_api_return_t code)
{
	if (code == EGIS_API_ERROR_IO_SPI) {
		return FINGERPRINT_ERROR_SPI_COMM;
	} else if (code == EGIS_API_ERROR_DEVICE_NOT_FOUND) {
		return FINGERPRINT_ERROR_BAD_HWID;
	} else if (code != EGIS_API_OK) {
		return FINGERPRINT_ERROR_INIT_FAIL;
	}
	return 0;
}

static int egis630_init(const struct device *dev)
{
	const struct egis630_cfg *cfg = dev->config;
	struct egis630_data *data = dev->data;
	egis_api_return_t ret;
	struct egis630_calibration_data *calibration_data;

	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;

	egis_fp_reset_sensor(cfg);

	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return 0;
	}

	int int_pin_value = gpio_pin_get_dt(&cfg->interrupt);

	ret = egis_sensor_init();

	data->errors |= convert_egis_sensor_init_error_code(ret);

	if (int_pin_value == gpio_pin_get_dt(&cfg->interrupt)) {
		LOG_ERR("Sensor IRQ not ready");
		data->errors |= FINGERPRINT_ERROR_NO_IRQ;
	}

	if (!ret && (cfg->calibration_data_addr != 0)) {
		calibration_data = (struct egis630_calibration_data
					    *)(cfg->calibration_data_addr +
					       CONFIG_FLASH_BASE_ADDRESS);
		ret = egis_apply_calibration_data(calibration_data->data,
						  calibration_data->size);
		if (ret != EGIS_API_OK) {
			LOG_WRN("Failed to apply calibration data from flash");
		}
	}

	return 0;
}

static int egis630_deinit(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return 0;
	}

	egis_api_return_t ret = egis_sensor_deinit();
	if (ret < 0) {
		LOG_ERR("egis_sensor_deinit() failed, result %d", ret);
		return ret;
	}

	return 0;
}

static int egis630_config(const struct device *dev, fingerprint_callback_t cb)
{
	struct egis630_data *data = dev->data;

	data->callback = cb;

	return 0;
}

static int egis630_get_info(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params image_frame_params_array[],
	uint8_t *num_params)
{
	const struct egis630_cfg *cfg = dev->config;
	struct egis630_data *data = dev->data;
	uint16_t sensor_id = 0;
	egis_api_return_t res = EGIS_API_OK;

	if (sensor_info == NULL || num_params == NULL ||
	    image_frame_params_array == NULL) {
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

	if (IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		res = egis_get_hwid(&sensor_id);
	}

	if (res != EGIS_API_OK) {
		LOG_ERR("Failed to get EGIS HWID: %d", res);
		return res;
	}

	sensor_info->model_id = sensor_id;
	sensor_info->errors = data->errors;

	return 0;
}

static int egis630_maintenance(const struct device *dev, uint8_t *buf,
			       size_t size)
{
	if (size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE) {
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	return 0;
}

static int egis630_set_mode(const struct device *dev,
			    enum fingerprint_sensor_mode mode)
{
	int rc = 0;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		if (IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
			LOG_INF("Sensor changes mode to finger detect");
			egis_set_detect_mode();
			rc = egis630_enable_irq(dev);
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		if (IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
			egis_sensor_power_down();
			rc = egis630_disable_irq(dev);
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		rc = egis630_disable_irq(dev);
		break;

	default:
		rc = -ENOTSUP;
	}

	return rc;
}

static int egis630_acquire_image(const struct device *dev,
				 enum fingerprint_capture_type capture_type,
				 uint8_t *image_buf, size_t image_buf_size)
{
	if (image_buf_size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE)
		return -EINVAL;

	egis_capture_mode_t egis_capture_type =
		convert_fp_capture_type_to_egis_capture_type(capture_type);

	if (egis_capture_type == EGIS_CAPTURE_TYPE_INVALID) {
		LOG_ERR("Unsupported capture_type %d provided", capture_type);
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	int ret = convert_egis_get_image_error_code(egis_get_image_with_mode(
		image_buf, image_buf_size, egis_capture_type));
	if (ret < 0) {
		LOG_ERR("Failed to acquire image with capture_type %d: %d",
			capture_type, ret);
	}

	return ret;
}

static int egis630_finger_status(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	egis_api_return_t ret = egis_check_int_status();

	switch (ret) {
	case EGIS_API_FINGER_PRESENT:
		return FINGERPRINT_FINGER_STATE_PRESENT;
	case EGIS_API_FINGER_LOST:
	default:
		return FINGERPRINT_FINGER_STATE_NONE;
	}
}

static const struct fingerprint_driver_api cros_fp_egis630_driver_api = {
	.init = egis630_init,
	.deinit = egis630_deinit,
	.config = egis630_config,
	.get_info = egis630_get_info,
	.maintenance = egis630_maintenance,
	.set_mode = egis630_set_mode,
	.acquire_image = egis630_acquire_image,
	.finger_status = egis630_finger_status,
};

static void egis630_irq(const struct device *dev, struct gpio_callback *cb,
			uint32_t pins)
{
	struct egis630_data *data =
		CONTAINER_OF(cb, struct egis630_data, irq_cb);

	egis630_disable_irq(data->dev);

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

static int egis630_init_driver(const struct device *dev)
{
	const struct egis630_cfg *cfg = dev->config;
	struct egis630_data *data = dev->data;
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

	data->dev = dev;
	gpio_init_callback(&data->irq_cb, egis630_irq, BIT(cfg->interrupt.pin));
	gpio_add_callback_dt(&cfg->interrupt, &data->irq_cb);

	return 0;
}

#define EGIS630_SENSOR_INFO(inst)                                          \
	{                                                                  \
		.vendor_id = FOURCC('E', 'G', 'I', 'S'),                   \
		.product_id = 9,                                           \
		.model_id = 1,                                             \
		.version = 1,                                              \
		.num_capture_types =                                       \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)), \
	}

#define EGIS630_IMAGE_PARAM_INITIALIZER(idx, inst)                             \
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

#define EGIS630_BUILD_ASSERT_IMAGE_SIZE(idx, inst)                             \
	BUILD_ASSERT(                                                          \
		CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE >=                        \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)), \
		"FP image buffer size smaller than raw image size at index " #idx);

#define EGIS630_DEFINE(inst)                                                         \
	static struct egis630_data egis630_data_##inst;                              \
	static const struct egis630_cfg egis630_cfg_##inst = {                       \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER |               \
							  SPI_WORD_SET(8)),          \
		.interrupt = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                 \
		.reset_pin = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),               \
		.calibration_data_addr = COND_CODE_1(                                \
			DT_NODE_EXISTS(                                              \
				DT_INST_PHANDLE(inst, calibration_data)),            \
			(DT_REG_ADDR(DT_INST_PHANDLE(inst, calibration_data))),      \
			(0)),                                                        \
		.sensor_info = EGIS630_SENSOR_INFO(inst),                            \
		.sensor_image_configs = { LISTIFY(                                   \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),           \
			EGIS630_IMAGE_PARAM_INITIALIZER, (, ), inst) },              \
	};                                                                           \
	LISTIFY(FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),                   \
		EGIS630_BUILD_ASSERT_IMAGE_SIZE, (;), inst)                          \
	BUILD_ASSERT(                                                                \
		FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)) <=                 \
			NUM_IMAGE_CAPTURE_TYPES,                                     \
		"EGIS630: Number of image configs exceeds NUM_IMAGE_CAPTURE_TYPES"); \
	DEVICE_DT_INST_DEFINE(inst, egis630_init_driver, NULL,                       \
			      &egis630_data_##inst, &egis630_cfg_##inst,             \
			      POST_KERNEL,                                           \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,               \
			      &cros_fp_egis630_driver_api)

DT_INST_FOREACH_STATUS_OKAY(EGIS630_DEFINE);
