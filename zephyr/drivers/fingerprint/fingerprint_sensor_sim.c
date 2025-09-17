/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_fingerprint_sensor_sim

#include "fingerprint_sensor_sim.h"

#include <zephyr/logging/log.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>
#include <fpsensor_driver.h>

LOG_MODULE_REGISTER(fp_sensor_simulator, LOG_LEVEL_INF);

#if !defined(CONFIG_TEST)
#error "Fingerprint sensor simulator should be used only in test environment"
#endif

static int fp_simulator_init(const struct device *dev)
{
	struct fp_simulator_data *data = dev->data;

	LOG_INF("Initializing fingerprint sensor simulator.");
	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;

	return data->state.init_result;
}

static int fp_simulator_deinit(const struct device *dev)
{
	struct fp_simulator_data *data = dev->data;

	return data->state.deinit_result;
}

static int fp_simulator_get_info(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params image_frame_params_array[],
	uint8_t *num_params)
{
	const struct fp_simulator_cfg *cfg = dev->config;
	struct fp_simulator_data *data = dev->data;

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

	sensor_info->errors = data->errors;

	return data->state.get_info_result;
}

static int fp_simulator_config(const struct device *dev,
			       fingerprint_callback_t cb)
{
	struct fp_simulator_data *data = dev->data;

	data->callback = cb;

	return data->state.config_result;
}

static int fp_simulator_maintenance(const struct device *dev, uint8_t *buf,
				    size_t size)
{
	struct fp_simulator_data *data = dev->data;

	data->state.maintenance_ran = true;
	data->errors &= ~FINGERPRINT_ERROR_DEAD_PIXELS_MASK;
	data->errors |= FINGERPRINT_ERROR_DEAD_PIXELS(
		min(data->state.bad_pixels, FINGERPRINT_ERROR_DEAD_PIXELS_MAX));

	return 0;
}

static int fp_simulator_set_mode(const struct device *dev,
				 enum fingerprint_sensor_mode mode)
{
	struct fp_simulator_data *data = dev->data;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		data->state.detect_mode = true;
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		data->state.low_power_mode = true;
		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		data->state.maintenance_ran = false;
		data->state.detect_mode = false;
		break;

	default:
		return -ENOTSUP;
	}

	return 0;
}

static int fp_simulator_acquire_image(const struct device *dev,
				      enum fingerprint_capture_type mode,
				      uint8_t *image_buf, size_t image_buf_size)
{
	const struct fp_simulator_cfg *config = dev->config;
	struct fp_simulator_data *data = dev->data;
	uint32_t frame_size = 0;

	for (uint8_t i = 0; i < config->sensor_info.num_capture_types; ++i) {
		if (config->sensor_image_configs[i].fp_capture_type == mode) {
			frame_size = config->sensor_image_configs[i].frame_size;
			break;
		}
	}

	size_t size = min(frame_size, image_buf_size);

	data->state.last_acquire_image_mode = mode;

	if (data->state.acquire_image_result == FINGERPRINT_SENSOR_SCAN_GOOD)
		memcpy(image_buf, config->image_buffer, size);

	return data->state.acquire_image_result;
}

static int fp_simulator_finger_status(const struct device *dev)
{
	struct fp_simulator_data *data = dev->data;

	return data->state.finger_state;
}

static DEVICE_API(fingerprint, fp_simulator_driver_api) = {
	.init = fp_simulator_init,
	.deinit = fp_simulator_deinit,
	.config = fp_simulator_config,
	.get_info = fp_simulator_get_info,
	.maintenance = fp_simulator_maintenance,
	.set_mode = fp_simulator_set_mode,
	.acquire_image = fp_simulator_acquire_image,
	.finger_status = fp_simulator_finger_status,
};

static int fp_simulator_init_driver(const struct device *dev)
{
	return 0;
}

#define FP_SIMULATOR_SENSOR_INFO(inst)                                     \
	{                                                                  \
		.vendor_id = FOURCC('C', 'r', 'O', 'S'),                   \
		.product_id = 0,                                           \
		.model_id = 0,                                             \
		.version = 0,                                              \
		.num_capture_types =                                       \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)), \
	}

#define FP_SIMULATOR_IMAGE_PARAM_INITIALIZER(idx, inst)                        \
	{                                                                      \
		.frame_size =                                                  \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)), \
		.pixel_format = FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(          \
			idx, DT_DRV_INST(inst)),                               \
		.width = FINGERPRINT_SENSOR_RES_X(idx, DT_DRV_INST(inst)),     \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, DT_DRV_INST(inst)),    \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, DT_DRV_INST(inst)),     \
		.fp_capture_type = FINGERPRINT_SENSOR_CAPTURE_TYPE(            \
			idx, DT_DRV_INST(inst)),                               \
		.reserved = 0,                                                 \
	}

#define FP_SIMULATOR_DEFINE(inst)                                            \
	static uint8_t fp_simulator_image_buffer_##inst[MAX_FRAME_SIZE(      \
		DT_DRV_INST(inst))];                                         \
	static struct fp_simulator_data fp_simulator_data_##inst;            \
	static const struct fp_simulator_cfg fp_simulator_cfg_##inst = {     \
		.image_buffer = fp_simulator_image_buffer_##inst,            \
		.sensor_info = FP_SIMULATOR_SENSOR_INFO(inst),               \
		.sensor_image_configs = { LISTIFY(                           \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),   \
			FP_SIMULATOR_IMAGE_PARAM_INITIALIZER, (, ), inst) }, \
	};                                                                   \
	DEVICE_DT_INST_DEFINE(inst, fp_simulator_init_driver, NULL,          \
			      &fp_simulator_data_##inst,                     \
			      &fp_simulator_cfg_##inst, POST_KERNEL,         \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,       \
			      &fp_simulator_driver_api)

DT_INST_FOREACH_STATUS_OKAY(FP_SIMULATOR_DEFINE);

/* Extensions to fingerprint sensor API. */
void z_impl_fingerprint_set_state(const struct device *dev,
				  struct fingerprint_sensor_state *state)
{
	struct fp_simulator_data *data = dev->data;

	data->state = *state;
}

void z_impl_fingerprint_get_state(const struct device *dev,
				  struct fingerprint_sensor_state *state)
{
	struct fp_simulator_data *data = dev->data;

	*state = data->state;
}

void z_impl_fingerprint_run_callback(const struct device *dev)
{
	struct fp_simulator_data *data = dev->data;

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

void z_impl_fingerprint_load_image(const struct device *dev, uint8_t *image,
				   size_t image_size)
{
	const struct fp_simulator_cfg *config = dev->config;
	size_t size = min(FP_SENSOR_IMAGE_SIZE, image_size);

	memcpy(config->image_buffer, image, size);
}
