/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT fpc_fpc1025

#include "fpc1025.h"
#include "fpc1025_private.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>

LOG_MODULE_REGISTER(cros_fingerprint, LOG_LEVEL_INF);

/* Provide information about used sensor. */
const struct fpc_sensor_info fpc_sensor_info = {
	.sensor = &fpc_bep_sensor_1025,
	.image_buffer_size = CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE,
};

/* Sensor IC commands */
enum fpc1025_cmd {
	FPC1025_CMD_DEEPSLEEP = 0x2C,
	FPC1025_CMD_HW_ID = 0xFC,
};

/* The 16-bit hardware ID is 0x021y */
#define FP_SENSOR_HWID_FPC 0x021

static int fpc1025_send_cmd(const struct device *dev, uint8_t cmd)
{
	const struct fpc1025_cfg *cfg = dev->config;
	const struct spi_buf tx_buf[1] = { { .buf = &cmd, .len = 1 } };
	const struct spi_buf_set tx = { .buffers = tx_buf, .count = 1 };
	int rc = spi_write_dt(&cfg->spi, &tx);

	/* Release CS line */
	spi_release_dt(&cfg->spi);

	return rc;
}

static int fpc1025_get_hwid(const struct device *dev, uint16_t *id)
{
	const struct fpc1025_cfg *cfg = dev->config;
	uint8_t cmd = FPC1025_CMD_HW_ID;
	uint8_t tmp;
	int rc;

	const struct spi_buf tx_buf[1] = { { .buf = &cmd, .len = 1 } };
	const struct spi_buf rx_buf[2] = { { .buf = &tmp, .len = 1 },
					   { .buf = id, .len = 2 } };
	const struct spi_buf_set tx = { .buffers = tx_buf,
					.count = ARRAY_SIZE(tx_buf) };
	const struct spi_buf_set rx = { .buffers = rx_buf,
					.count = ARRAY_SIZE(rx_buf) };

	if (id == NULL)
		return -EINVAL;

	rc = spi_transceive_dt(&cfg->spi, &tx, &rx);

	/* Release CS line */
	spi_release_dt(&cfg->spi);

	/* HWID is in big endian, so convert it CPU endianness. */
	*id = sys_be16_to_cpu(*id);

	return rc;
}

static inline int fpc1025_enable_irq(const struct device *dev)
{
	const struct fpc1025_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					     GPIO_INT_EDGE_TO_ACTIVE);
	if (rc < 0) {
		LOG_ERR("Can't enable interrupt: %d", rc);
	}

	return rc;
}

static inline int fpc1025_disable_irq(const struct device *dev)
{
	const struct fpc1025_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt, GPIO_INT_DISABLE);
	if (rc < 0) {
		LOG_ERR("Can't disable interrupt: %d", rc);
	}

	return rc;
}

static int fpc1025_set_mode(const struct device *dev,
			    enum fingerprint_sensor_mode mode)
{
	int rc = 0, rc_cmd;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		if (IS_ENABLED(CONFIG_HAVE_FPC1025_PRIVATE_DRIVER)) {
			fp_sensor_configure_detect();
			rc = fpc1025_enable_irq(dev);
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		rc = fpc1025_disable_irq(dev);
		rc_cmd = fpc1025_send_cmd(dev, FPC1025_CMD_DEEPSLEEP);

		if (rc == 0) {
			rc = rc_cmd;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		rc = fpc1025_disable_irq(dev);
		break;

	default:
		rc = -ENOTSUP;
	}

	return rc;
}

static int fpc1025_init(const struct device *dev)
{
	struct fpc1025_data *data = dev->data;
	uint16_t id = 0;
	int rc;

	if (IS_ENABLED(CONFIG_HAVE_FPC1025_PRIVATE_DRIVER)) {
		/* Print the binary libfpbep.a library version. */
		LOG_PRINTK("FPC libfpbep.a %s\n", fp_sensor_get_version());

		/* Print the BEP version and build time of the library. */
		LOG_PRINTK("Build information - %s\n",
			   fp_sensor_get_build_info());
	}

	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;

	rc = fpc1025_get_hwid(dev, &id);
	if (rc) {
		LOG_ERR("Failed to get FPC HWID: %d", rc);
		return rc;
	}

	if ((id >> 4) != FP_SENSOR_HWID_FPC) {
		LOG_ERR("FPC unknown silicon 0x%04x", id);
		return -EINVAL;
	}

	LOG_PRINTK("FPC1025 id 0x%04x\n", id);

	if (IS_ENABLED(CONFIG_HAVE_FPC1025_PRIVATE_DRIVER)) {
		rc = fp_sensor_open();
		if (rc) {
			LOG_ERR("fp_sensor_open() failed, result %d", rc);
			data->errors |= FINGERPRINT_ERROR_INIT_FAIL;
			return rc;
		}
	}

	fpc1025_set_mode(dev, FINGERPRINT_SENSOR_MODE_LOW_POWER);

	return 0;
}

static int fpc1025_deinit(const struct device *dev)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_FPC1025_PRIVATE_DRIVER)) {
		return 0;
	}

	rc = fp_sensor_close();
	if (rc < 0) {
		LOG_ERR("fp_sensor_close() failed, result %d", rc);
		return rc;
	}

	return 0;
}

static int fpc1025_get_info(const struct device *dev,
			    struct fingerprint_info *info)
{
	const struct fpc1025_cfg *cfg = dev->config;
	struct fpc1025_data *data = dev->data;
	uint16_t id = 0;
	int rc;

	/* Copy immutable sensor information to the structure. */
	memcpy(info, &cfg->info, sizeof(struct fingerprint_info));

	rc = fpc1025_get_hwid(dev, &id);
	if (rc) {
		LOG_ERR("Failed to get FPC HWID: %d", rc);
		return rc;
	}

	info->model_id = id;
	info->errors = data->errors;

	return 0;
}

static int fpc1025_config(const struct device *dev, fingerprint_callback_t cb)
{
	struct fpc1025_data *data = dev->data;

	data->callback = cb;

	return 0;
}

static int fpc1025_maintenance(const struct device *dev, uint8_t *buf,
			       size_t size)
{
	struct fpc1025_data *data = dev->data;
	struct fp_sensor_info sensor_info;
	uint64_t start;
	int rc = 0;

	if (!IS_ENABLED(CONFIG_HAVE_FPC1025_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	if (size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE)
		return -EINVAL;

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

	data->errors |=
		FINGERPRINT_ERROR_DEAD_PIXELS(sensor_info.num_defective_pixels);
	LOG_INF("num_defective_pixels: %d", sensor_info.num_defective_pixels);

	return 0;
}

BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_GOOD == FPC_SENSOR_GOOD_IMAGE_QUALITY);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY ==
	     FPC_SENSOR_LOW_IMAGE_QUALITY);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_TOO_FAST == FPC_SENSOR_TOO_FAST);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE ==
	     FPC_SENSOR_LOW_COVERAGE);

static int fpc1025_acquire_image(const struct device *dev, int mode,
				 uint8_t *image_buf, size_t image_buf_size)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_FPC1025_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	if (image_buf_size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE)
		return -EINVAL;

	rc = fp_sensor_acquire_image_with_mode(image_buf, mode);
	if (rc < 0) {
		LOG_ERR("Failed to acquire image with mode %d: %d", mode, rc);
		return rc;
	}

	/*
	 * Finger status codes returned by fp_sensor_acquire_image() are
	 * synchronized with FINGERPRINT_SENSOR_* defines.
	 */
	return rc;
}

BUILD_ASSERT(FINGERPRINT_FINGER_STATE_NONE == FPC_FINGER_NONE);
BUILD_ASSERT(FINGERPRINT_FINGER_STATE_PARTIAL == FPC_FINGER_PARTIAL);
BUILD_ASSERT(FINGERPRINT_FINGER_STATE_PRESENT == FPC_FINGER_PRESENT);

static int fpc1025_finger_status(const struct device *dev)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_FPC1025_PRIVATE_DRIVER)) {
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

static const struct fingerprint_driver_api cros_fp_fpc1025_driver_api = {
	.init = fpc1025_init,
	.deinit = fpc1025_deinit,
	.config = fpc1025_config,
	.get_info = fpc1025_get_info,
	.maintenance = fpc1025_maintenance,
	.set_mode = fpc1025_set_mode,
	.acquire_image = fpc1025_acquire_image,
	.finger_status = fpc1025_finger_status,
};

static void fpc1025_irq(const struct device *dev, struct gpio_callback *cb,
			uint32_t pins)
{
	struct fpc1025_data *data =
		CONTAINER_OF(cb, struct fpc1025_data, irq_cb);

	fpc1025_disable_irq(data->dev);

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

static int fpc1025_init_driver(const struct device *dev)
{
	const struct fpc1025_cfg *cfg = dev->config;
	struct fpc1025_data *data = dev->data;
	int ret;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("SPI bus is not ready");
		return -EINVAL;
	}

	if (!gpio_is_ready_dt(&cfg->reset_pin)) {
		LOG_ERR("Port for sensor reset GPIO is not ready");
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->reset_pin, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Can't configure sensor reset pin");
		return ret;
	}

	if (!gpio_is_ready_dt(&cfg->interrupt)) {
		LOG_ERR("Port for interrupt GPIO is not ready");
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->interrupt, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Can't configure interrupt pin");
		return ret;
	}

	data->dev = dev;
	gpio_init_callback(&data->irq_cb, fpc1025_irq, BIT(cfg->interrupt.pin));
	gpio_add_callback_dt(&cfg->interrupt, &data->irq_cb);

	return 0;
}

#define FPC1025_SENSOR_INFO(inst)                                         \
	{                                                                 \
		.vendor_id = FOURCC('F', 'P', 'C', ' '), .product_id = 9, \
		.model_id = 1, .version = 1,                              \
		.frame_size = CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE,       \
		.pixel_format = FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(     \
			DT_DRV_INST(inst)),                               \
		.width = FINGERPRINT_SENSOR_RES_X(DT_DRV_INST(inst)),     \
		.height = FINGERPRINT_SENSOR_RES_Y(DT_DRV_INST(inst)),    \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(DT_DRV_INST(inst)),     \
	}

#define FPC1025_DEFINE(inst)                                                   \
	static struct fpc1025_data fpc1025_data_##inst;                        \
	static const struct fpc1025_cfg fpc1025_cfg_##inst = {                 \
		.spi = SPI_DT_SPEC_INST_GET(                                   \
			inst,                                                  \
			SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |                 \
				SPI_HOLD_ON_CS | SPI_LOCK_ON,                  \
			0),                                                    \
		.interrupt = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),           \
		.reset_pin = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),         \
		.info = FPC1025_SENSOR_INFO(inst),                             \
	};                                                                     \
	BUILD_ASSERT(                                                          \
		CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE >=                        \
			FINGERPRINT_SENSOR_REAL_IMAGE_SIZE(DT_DRV_INST(inst)), \
		"FP image buffer size is smaller than raw image size");        \
	DEVICE_DT_INST_DEFINE(inst, fpc1025_init_driver, NULL,                 \
			      &fpc1025_data_##inst, &fpc1025_cfg_##inst,       \
			      POST_KERNEL,                                     \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,         \
			      &cros_fp_fpc1025_driver_api)

DT_INST_FOREACH_STATUS_OKAY(FPC1025_DEFINE);
