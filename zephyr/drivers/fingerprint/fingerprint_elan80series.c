/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fingerprint_elan80series.h"
#include "fingerprint_elan80series_pal.h"
#include "fingerprint_elan80series_private.h"

#include <assert.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/minmax.h>
#include <zephyr/sys/util.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>

LOG_MODULE_REGISTER(cros_fingerprint, LOG_LEVEL_INF);

COND_CODE_1(CONFIG_ZTEST, (), (static))
enum elan_capture_type
convert_fp_capture_type_to_elan_capture_type(enum fingerprint_capture_type mode)
{
	switch (mode) {
	case FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT:
		return ELAN_CAPTURE_VENDOR_FORMAT;
	case FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE:
		return ELAN_CAPTURE_SIMPLE_IMAGE;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN0:
		return ELAN_CAPTURE_PATTERN0;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN1:
		return ELAN_CAPTURE_PATTERN1;
	case FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST:
		return ELAN_CAPTURE_QUALITY_TEST;
	case FINGERPRINT_CAPTURE_TYPE_RESET_TEST:
		return ELAN_CAPTURE_RESET_TEST;
	default:
		return ELAN_CAPTURE_TYPE_INVALID;
	}
}

static void elan80series_use_flash_addresses(const struct device *dev)
{
#if DT_ANY_INST_HAS_PROP_STATUS_OKAY(base_image) && \
	DT_ANY_INST_HAS_PROP_STATUS_OKAY(ft_info)
	const struct elan80series_cfg *cfg = dev->config;

	if (cfg->base_image_addr != 0 && cfg->ft_info_addr != 0) {
		use_flash_addresses(cfg->base_image_addr, cfg->ft_info_addr);
	}
#endif
}

static int elan80series_get_hwid(const struct device *dev, uint16_t *id)
{
	int rc;
	uint8_t id_hi = 0, id_lo = 0;

	if (id == NULL)
		return -EINVAL;

	rc = elan_read_register(HWID_HI, &id_hi);
	rc |= elan_read_register(HWID_LO, &id_lo);
	if (rc) {
		LOG_ERR("ELAN HW ID read failed %d", rc);
		return -ENOTSUP;
	}
	*id = (id_hi << 8) | id_lo;

	return 0;
}

static int elan80series_check_hwid(const struct device *dev)
{
	struct elan80series_data *data = dev->data;
	uint16_t id = 0;
	int status;

	status = elan80series_get_hwid(dev, &id);
	if (status < 0) {
		assert(status == -ENOTSUP);
		data->errors |= FINGERPRINT_ERROR_SPI_COMM;
	}

	if (id != FP_SENSOR_HWID_ELAN) {
		LOG_ERR("ELAN unknown silicon 0x%04x", id);
		data->errors |= FINGERPRINT_ERROR_BAD_HWID;
		return -ENOTSUP;
	}

	LOG_INF("ELAN HWID 0x%04x", id);
	return 0;
}

static inline int elan80series_enable_irq(const struct device *dev)
{
	const struct elan80series_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					     GPIO_INT_EDGE_TO_ACTIVE);
	if (rc < 0) {
		LOG_ERR("Can't enable interrupt: %d", rc);
	}

	return rc;
}

static inline int elan80series_disable_irq(const struct device *dev)
{
	const struct elan80series_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt, GPIO_INT_DISABLE);
	if (rc < 0) {
		LOG_ERR("Can't disable interrupt: %d", rc);
	}

	return rc;
}

static int elan80series_init(const struct device *dev)
{
	struct elan80series_data *data = dev->data;
	int rc;

	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;

	if (IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
		elan_execute_reset();
		elan_alg_param_setting();
		elan80series_use_flash_addresses(dev);
	}
	elan_set_hv_chip(true);

	rc = elan80series_check_hwid(dev);
	if (rc != 0) {
		data->errors |= FINGERPRINT_ERROR_INIT_FAIL;
		return rc;
	}

	if (elan_execute_calibration() < 0)
		data->errors |= FINGERPRINT_ERROR_INIT_FAIL;
	if (IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
		if (elan_woe_mode() != 0)
			data->errors |= FINGERPRINT_ERROR_SPI_COMM;
	}

	return 0;
}

static int elan80series_deinit(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
		return 0;
	}

	int rc = elan_fp_deinit();

	if (rc != 0) {
		LOG_ERR("elan_sensor_deinit() failed, result %d", rc);
		return rc;
	}

	return 0;
}

static int elan80series_get_info(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params image_frame_params_array[],
	uint8_t *num_params)
{
	const struct elan80series_cfg *cfg = dev->config;
	struct elan80series_data *data = dev->data;
	uint16_t id = 0;

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

	if (elan80series_get_hwid(dev, &id)) {
		return -EINVAL;
	}

	sensor_info->model_id = id;
	sensor_info->errors = data->errors;

	return 0;
}

static int elan80series_config(const struct device *dev,
			       fingerprint_callback_t cb)
{
	struct elan80series_data *data = dev->data;

	data->callback = cb;

	return 0;
}

static int elan80series_maintenance(const struct device *dev, uint8_t *buf,
				    size_t size)
{
	if (size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE) {
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
		return 0;
	}

	int rv;
	struct elan80series_data *data = dev->data;
	fp_sensor_info_t sensor_info;
	uint32_t start = k_uptime_get_32();
	uint32_t end;

	/* Initial status */
	data->errors &= 0xFC00;
	sensor_info.num_defective_pixels = 0;
	sensor_info.sensor_error_code = 0;
	rv = elan_fp_sensor_maintenance(&sensor_info);
	end = k_ticks_to_ms_near32(k_uptime_ticks());
	LOG_INF("Maintenance took %d ms", end - start);

	if (rv != 0) {
		/*
		 * Failure can occur if any of the fingerprint detection zones
		 * are covered (i.e., finger is on sensor).
		 */
		LOG_ERR("Failed to run maintenance: %d", rv);
		return -ENOTSUP;
	}

	/*
	 * Reset the number of dead pixels before any update.
	 */
	data->errors &= ~FINGERPRINT_ERROR_DEAD_PIXELS_MASK;
	data->errors |= FINGERPRINT_ERROR_DEAD_PIXELS(
		min(sensor_info.num_defective_pixels,
		    FINGERPRINT_ERROR_DEAD_PIXELS_MAX));
	LOG_INF("num_defective_pixels: %d", sensor_info.num_defective_pixels);
	LOG_INF("sensor_error_code: %d", sensor_info.sensor_error_code);

	return 0;
}

static int elan80series_set_mode(const struct device *dev,
				 enum fingerprint_sensor_mode mode)
{
	int rc = 0;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		if (IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
			rc = elan_woe_mode();
			if (rc == 0) {
				rc = elan80series_enable_irq(dev);
			}
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		if (IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
			rc = elan_woe_mode();
			if (rc == 0) {
				rc = elan80series_disable_irq(dev);
			}
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		rc = elan80series_disable_irq(dev);
		break;

	default:
		rc = -ENOTSUP;
	}

	return rc;
}

BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_GOOD == FP_SENSOR_GOOD_IMAGE_QUALITY);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY ==
	     FP_SENSOR_LOW_IMAGE_QUALITY);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_TOO_FAST == FP_SENSOR_TOO_FAST);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE ==
	     FP_SENSOR_LOW_COVERAGE);

static int
elan80series_acquire_image(const struct device *dev,
			   enum fingerprint_capture_type capture_type,
			   uint8_t *image_buf, size_t image_buf_size)
{
	if (image_buf_size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE) {
		return -EINVAL;
	}

	enum elan_capture_type rc =
		convert_fp_capture_type_to_elan_capture_type(capture_type);

	if (rc == ELAN_CAPTURE_TYPE_INVALID) {
		LOG_ERR("Unsupported capture_type %d provided", capture_type);
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rc = elan_sensor_acquire_image_with_mode(image_buf, rc);
	if (rc < 0) {
		LOG_ERR("Failed to acquire image with mode %d: %d",
			capture_type, rc);
		return -EFAULT;
	}

	return rc;
}

BUILD_ASSERT((int)FINGERPRINT_FINGER_STATE_NONE == (int)FINGER_NONE);
BUILD_ASSERT((int)FINGERPRINT_FINGER_STATE_PARTIAL == (int)FINGER_PARTIAL);
BUILD_ASSERT((int)FINGERPRINT_FINGER_STATE_PRESENT == (int)FINGER_PRESENT);

static int elan80series_finger_status(const struct device *dev)
{
	enum finger_state rc;

	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rc = elan_sensor_finger_status();
	if (rc < 0) {
		LOG_ERR("Failed to get finger status: %d", rc);
		return rc;
	}

	return rc;
}

static DEVICE_API(fingerprint, cros_fp_elan80series_driver_api) = {
	.init = elan80series_init,
	.deinit = elan80series_deinit,
	.config = elan80series_config,
	.get_info = elan80series_get_info,
	.maintenance = elan80series_maintenance,
	.set_mode = elan80series_set_mode,
	.acquire_image = elan80series_acquire_image,
	.finger_status = elan80series_finger_status,
};

static void elan80series_irq(const struct device *dev, struct gpio_callback *cb,
			     uint32_t pins)
{
	struct elan80series_data *data =
		CONTAINER_OF(cb, struct elan80series_data, irq_cb);

	elan80series_disable_irq(data->dev);

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

static int elan80series_init_driver(const struct device *dev)
{
	const struct elan80series_cfg *cfg = dev->config;
	struct elan80series_data *data = dev->data;
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
	gpio_init_callback(&data->irq_cb, elan80series_irq,
			   BIT(cfg->interrupt.pin));
	gpio_add_callback_dt(&cfg->interrupt, &data->irq_cb);

	return 0;
}

#define ELAN80SERIES_SENSOR_INFO(inst)                                     \
	{                                                                  \
		.vendor_id = FOURCC('E', 'L', 'A', 'N'),                   \
		.product_id = PID,                                         \
		.model_id = MID,                                           \
		.version = VERSION,                                        \
		.num_capture_types =                                       \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)), \
	}

#define ELAN80SERIES_IMAGE_PARAM_INITIALIZER(idx, inst)                        \
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

#define ELAN80SERIES_BUILD_ASSERT_IMAGE_SIZE(idx, inst)                        \
	BUILD_ASSERT(                                                          \
		CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE >=                        \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)), \
		"FP image buffer size smaller than raw image size at index " #idx);

#define GET_PARTITION_ADDRESS(inst, prop_name)                        \
	COND_CODE_1(DT_NODE_EXISTS(DT_INST_PHANDLE(inst, prop_name)), \
		    (DT_REG_ADDR(DT_INST_PHANDLE(inst, prop_name)) +  \
		     CONFIG_FLASH_BASE_ADDRESS),                      \
		    (0))

#define ELAN80SERIES_DEFINE(inst)                                            \
	static struct elan80series_data elan80series_data_##inst;            \
	static const struct elan80series_cfg elan80series_cfg_##inst = {     \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER |       \
							  SPI_WORD_SET(8)),  \
		.base_image_addr = GET_PARTITION_ADDRESS(inst, base_image),  \
		.ft_info_addr = GET_PARTITION_ADDRESS(inst, ft_info),        \
		.interrupt = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),         \
		.reset_pin = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),       \
		.sensor_info = ELAN80SERIES_SENSOR_INFO(inst),               \
		.sensor_image_configs = { LISTIFY(                           \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),   \
			ELAN80SERIES_IMAGE_PARAM_INITIALIZER, (, ), inst) }, \
	};                                                                   \
	LISTIFY(FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),           \
		ELAN80SERIES_BUILD_ASSERT_IMAGE_SIZE, (;), inst)             \
	BUILD_ASSERT(FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)) <=    \
			     NUM_IMAGE_CAPTURE_TYPES,                        \
		     "ELAN80SERIES: Number of image configs exceeds "        \
		     "NUM_IMAGE_CAPTURE_TYPES");                             \
	DEVICE_DT_INST_DEFINE(inst, elan80series_init_driver, NULL,          \
			      &elan80series_data_##inst,                     \
			      &elan80series_cfg_##inst, POST_KERNEL,         \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,       \
			      &cros_fp_elan80series_driver_api)

DT_INST_FOREACH_STATUS_OKAY(ELAN80SERIES_DEFINE);
