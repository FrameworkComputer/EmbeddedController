/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT fpc_fpc1145

#include "fingerprint_fpc1145.h"
#include "fingerprint_fpc1145_private.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>

LOG_MODULE_REGISTER(cros_fingerprint, LOG_LEVEL_INF);

/* Sensor IC commands */
enum fpc1145_cmd {
	FPC1145_CMD_STATUS = 0x14,
	FPC1145_CMD_INT_STS = 0x18,
	FPC1145_CMD_INT_CLR = 0x1C,
	FPC1145_CMD_FINGER_QUERY = 0x20,
	FPC1145_CMD_SLEEP = 0x28,
	FPC1145_CMD_DEEPSLEEP = 0x2C,
	FPC1145_CMD_SOFT_RESET = 0xF8,
	FPC1145_CMD_HW_ID = 0xFC,
};

int convert_fp_capture_type_to_fpc_capture_type(
	enum fingerprint_capture_type mode)
{
	switch (mode) {
	case FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT:
		return FPC_CAPTURE_VENDOR_FORMAT;
	case FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE:
		return FPC_CAPTURE_SIMPLE_IMAGE;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN0:
		return FPC_CAPTURE_PATTERN0;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN1:
		return FPC_CAPTURE_PATTERN1;
	case FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST:
		return FPC_CAPTURE_QUALITY_TEST;
	case FINGERPRINT_CAPTURE_TYPE_RESET_TEST:
		return FPC_CAPTURE_RESET_TEST;
	default:
		return -EINVAL;
	}
}

/* Minimum reset duration */
#define FP_SENSOR_RESET_DURATION_US (10 * USEC_PER_MSEC)
/* Maximum number of attempts to initialise the sensor */
#define FP_SENSOR_MAX_INIT_ATTEMPTS 10
/* Delay between failed attempts of fp_sensor_open() */
#define FP_SENSOR_OPEN_DELAY_US (500 * USEC_PER_MSEC)
/**
 * The hardware ID is 16-bits. All 114x FPC sensors (including FPC1145) are
 * detected with the pattern 0x1400 and mask 0xFFF0. All supported variants of
 * the 1145 (0x140B, 0x140C, and 0x1401) should be detected as part of the FPC
 * 1140 family with identical functionality.
 * See http://b/150407388 for additional details.
 */
#define FP_SENSOR_HWID_FPC 0x140

static uint8_t cmd_nocache __nocache;
static uint8_t tmp_nocache __nocache;
static uint8_t buff_nocache[2] __nocache;
static struct k_sem nocache_buffs_lock;

static int fpc1145_get_hwid(const struct fpc1145_cfg *cfg, uint16_t *id)
{
	cmd_nocache = FPC1145_CMD_HW_ID;
	uint16_t *id_nocache = (uint16_t *)buff_nocache;
	__ASSERT(sizeof(buff_nocache) >= sizeof(uint16_t),
		 "Too small nocache buff.");
	int rc;

	const struct spi_buf tx_buf[1] = { { .buf = &cmd_nocache, .len = 1 } };
	const struct spi_buf rx_buf[2] = { { .buf = &tmp_nocache, .len = 1 },
					   { .buf = id_nocache, .len = 2 } };
	const struct spi_buf_set tx = { .buffers = tx_buf,
					.count = ARRAY_SIZE(tx_buf) };
	const struct spi_buf_set rx = { .buffers = rx_buf,
					.count = ARRAY_SIZE(rx_buf) };

	if (id == NULL)
		return -EINVAL;

	k_sem_take(&nocache_buffs_lock, K_FOREVER);
	rc = spi_transceive_dt(&cfg->spi, &tx, &rx);

	/* HWID is in big endian, so convert it CPU endianness. */
	*id = sys_be16_to_cpu(*id_nocache);
	k_sem_give(&nocache_buffs_lock);

	return rc;
}

static int fpc1145_check_hwid(const struct device *dev)
{
	const struct fpc1145_cfg *cfg = dev->config;
	struct fpc1145_data *data = dev->data;
	uint16_t id = 0;
	int rc;

	// TODO(b/361826387): Reconcile the different behavior and handling of
	// the |errors| global state between the libfp and bep implementations.
	/* Clear previous occurrences of relevant |errors| flags. */
	data->errors &=
		(~FINGERPRINT_ERROR_SPI_COMM & ~FINGERPRINT_ERROR_BAD_HWID);
	rc = fpc1145_get_hwid(cfg, &id);
	if (rc) {
		LOG_ERR("Failed to get FPC HWID: %d", rc);
		data->errors |= FINGERPRINT_ERROR_SPI_COMM;

		return rc;
	}

	if ((id >> 4) != FP_SENSOR_HWID_FPC) {
		LOG_ERR("FPC unknown silicon 0x%04x", id);
		data->errors |= FINGERPRINT_ERROR_BAD_HWID;
		return -EINVAL;
	}

	LOG_PRINTK("FPC1145 id 0x%04x\n", id);

	return 0;
}

static uint8_t fpc1145_read_clear_int(const struct fpc1145_cfg *cfg)
{
	cmd_nocache = FPC1145_CMD_INT_CLR;
	int rc;
	uint8_t ret;

	const struct spi_buf tx_buf[1] = { { .buf = &cmd_nocache, .len = 1 } };
	const struct spi_buf rx_buf[2] = { { .buf = &tmp_nocache, .len = 1 },
					   { .buf = &buff_nocache, .len = 1 } };
	const struct spi_buf_set tx = { .buffers = tx_buf,
					.count = ARRAY_SIZE(tx_buf) };
	const struct spi_buf_set rx = { .buffers = rx_buf,
					.count = ARRAY_SIZE(rx_buf) };

	k_sem_take(&nocache_buffs_lock, K_FOREVER);
	rc = spi_transceive_dt(&cfg->spi, &tx, &rx);

	ret = buff_nocache[0];
	k_sem_give(&nocache_buffs_lock);

	if (rc) {
		return 0xff;
	}

	return ret;
}

/*
 * Toggle the h/w reset pins and clear any pending IRQs before initializing the
 * sensor contexts.
 * Returns:
 * - 0 on success.
 * - Negative errno code on failure (and |errors| variable is updated where
 *   appropriate).
 */
static int fpc1145_pulse_hw_reset(const struct device *dev)
{
	int ret;
	int rc = 0;
	const struct fpc1145_cfg *cfg = dev->config;
	struct fpc1145_data *data = dev->data;

	/* Clear previous occurrence of possible error flags. */
	data->errors &= ~FINGERPRINT_ERROR_NO_IRQ;

	/* Ensure we pulse reset low to initiate the startup */
	ret = gpio_pin_set_dt(&cfg->reset_pin, 1);
	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
		return ret;
	}
	k_usleep(FP_SENSOR_RESET_DURATION_US);
	ret = gpio_pin_set_dt(&cfg->reset_pin, 0);
	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
		return ret;
	}
	/* the IRQ line should be set high by the sensor */
	k_usleep(FP_SENSOR_RESET_DURATION_US);
	ret = gpio_pin_get_dt(&cfg->interrupt);
	if (ret < 0) {
		LOG_ERR("Failed to get FP interrupt pin, status: %d", ret);
		return ret;
	}
	if (!ret) {
		LOG_ERR("Sensor IRQ not ready");
		data->errors |= FINGERPRINT_ERROR_NO_IRQ;
		rc = -EINVAL;
	}

	/* Check the Hardware ID */
	ret = fpc1145_check_hwid(dev);
	if (ret) {
		LOG_ERR("Failed to verify HW ID");
		rc = ret;
	}

	/* clear the pending 'ready' IRQ before enabling interrupts */
	fpc1145_read_clear_int(cfg);

	return rc;
}

static inline int fpc1145_enable_irq(const struct device *dev)
{
	const struct fpc1145_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					     GPIO_INT_EDGE_TO_ACTIVE);
	if (rc < 0) {
		LOG_ERR("Can't enable interrupt: %d", rc);
	}

	return rc;
}

static inline int fpc1145_disable_irq(const struct device *dev)
{
	const struct fpc1145_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt, GPIO_INT_DISABLE);
	if (rc < 0) {
		LOG_ERR("Can't disable interrupt: %d", rc);
	}

	return rc;
}

static int fpc1145_set_mode(const struct device *dev,
			    enum fingerprint_sensor_mode mode)
{
	int rc = 0;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		if (IS_ENABLED(CONFIG_HAVE_FPC1145_PRIVATE_DRIVER)) {
			fp_sensor_configure_detect();
			rc = fpc1145_enable_irq(dev);
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		/*
		 * TODO(b/117620462): verify that sleep mode is WAI (no
		 * increased latency, expected power consumption).
		 */
		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		rc = fpc1145_disable_irq(dev);
		break;

	default:
		rc = -ENOTSUP;
	}

	return rc;
}

static int fpc1145_init(const struct device *dev)
{
	struct fpc1145_data *data = dev->data;
	int attempt;
	int res;

	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;

	if (IS_ENABLED(CONFIG_HAVE_FPC1145_PRIVATE_DRIVER)) {
		/* Print the binary libfpsensor.a library version. */
		LOG_PRINTK("FPC libfpsensor.a v%s\n", fp_sensor_get_version());
	}

	attempt = 0;
	do {
		attempt++;

		res = fpc1145_pulse_hw_reset(dev);
		if (res) {
			/* In case of failure, retry after a delay. */
			LOG_ERR("H/W sensor reset atmept %d/%d failed, error flags: 0x%x",
				attempt, FP_SENSOR_MAX_INIT_ATTEMPTS,
				data->errors);
			k_usleep(FP_SENSOR_OPEN_DELAY_US);
			continue;
		}

		/*
		 * Ensure that any previous context data is obliterated in case
		 * of a sensor reset.
		 */
		memset(data->ctx, 0, FP_SENSOR_CONTEXT_SIZE);

		if (IS_ENABLED(CONFIG_HAVE_FPC1145_PRIVATE_DRIVER)) {
			res = fp_sensor_open(data->ctx, FP_SENSOR_CONTEXT_SIZE);
			LOG_INF("Sensor init (attempt %d): 0x%x", attempt, res);
		}

		/*
		 * Retry on failure. This typically happens if the user has left
		 * their finger on the sensor after powering up the device, DFD
		 * will fail in that case. We've seen other error modes in the
		 * field, retry in all cases to be more resilient.
		 */
		if (!res) {
			break;
		}

		k_usleep(FP_SENSOR_OPEN_DELAY_US);
	} while (attempt < FP_SENSOR_MAX_INIT_ATTEMPTS);

	if (res) {
		data->errors |= FINGERPRINT_ERROR_INIT_FAIL;
	}

	fpc1145_set_mode(dev, FINGERPRINT_SENSOR_MODE_LOW_POWER);

	return res;
}

static int fpc1145_deinit(const struct device *dev)
{
	/*
	 * TODO(tomhughes): libfp doesn't have fp_sensor_close like BEP does.
	 * We'll need FPC to either add it or verify that we don't have the same
	 * problem with the libfp library as described in:
	 * b/124773209#comment46
	 */
	return 0;
}

static int fpc1145_get_info(const struct device *dev,
			    struct fingerprint_info *info)
{
	const struct fpc1145_cfg *cfg = dev->config;
	struct fpc1145_data *data = dev->data;
	uint16_t id = 0;
	int rc;

	/* Copy immutable sensor information to the structure. */
	memcpy(info, &cfg->info, sizeof(struct fingerprint_info));

	rc = fpc1145_get_hwid(cfg, &id);
	if (rc) {
		LOG_ERR("Failed to get FPC HWID: %d", rc);
		return rc;
	}

	info->model_id = id;
	info->errors = data->errors;

	return 0;
}

static int fpc1145_config(const struct device *dev, fingerprint_callback_t cb)
{
	struct fpc1145_data *data = dev->data;

	data->callback = cb;

	return 0;
}

static int fpc1145_maintenance(const struct device *dev, uint8_t *buf,
			       size_t size)
{
	struct fpc1145_data *data = dev->data;
	struct fp_sensor_info sensor_info;
	uint64_t start;
	int rc = 0;

	if (!IS_ENABLED(CONFIG_HAVE_FPC1145_PRIVATE_DRIVER)) {
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

	data->errors &= ~FINGERPRINT_ERROR_DEAD_PIXELS_MASK;
	data->errors |= FINGERPRINT_ERROR_DEAD_PIXELS(
		MIN(sensor_info.num_defective_pixels,
		    FINGERPRINT_ERROR_DEAD_PIXELS_MAX));
	LOG_INF("num_defective_pixels: %d", sensor_info.num_defective_pixels);

	return 0;
}

BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_GOOD == FPC_SENSOR_GOOD_IMAGE_QUALITY);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY ==
	     FPC_SENSOR_LOW_IMAGE_QUALITY);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_TOO_FAST == FPC_SENSOR_TOO_FAST);
BUILD_ASSERT(FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE ==
	     FPC_SENSOR_LOW_COVERAGE);

static int fpc1145_acquire_image(const struct device *dev,
				 enum fingerprint_capture_type capture_type,
				 uint8_t *image_buf, size_t image_buf_size)
{
	int rc;

	if (image_buf_size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE)
		return -EINVAL;

	rc = convert_fp_capture_type_to_fpc_capture_type(capture_type);

	if (rc < 0) {
		LOG_ERR("Unsupported capture_type %d provided", capture_type);
		return rc;
	}

	if (!IS_ENABLED(CONFIG_HAVE_FPC1145_PRIVATE_DRIVER)) {
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

BUILD_ASSERT(FINGERPRINT_FINGER_STATE_NONE == FPC_FINGER_NONE);
BUILD_ASSERT(FINGERPRINT_FINGER_STATE_PARTIAL == FPC_FINGER_PARTIAL);
BUILD_ASSERT(FINGERPRINT_FINGER_STATE_PRESENT == FPC_FINGER_PRESENT);

static int fpc1145_finger_status(const struct device *dev)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_FPC1145_PRIVATE_DRIVER)) {
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

static const struct fingerprint_driver_api cros_fp_fpc1145_driver_api = {
	.init = fpc1145_init,
	.deinit = fpc1145_deinit,
	.config = fpc1145_config,
	.get_info = fpc1145_get_info,
	.maintenance = fpc1145_maintenance,
	.set_mode = fpc1145_set_mode,
	.acquire_image = fpc1145_acquire_image,
	.finger_status = fpc1145_finger_status,
};

static void fpc1145_irq(const struct device *dev, struct gpio_callback *cb,
			uint32_t pins)
{
	struct fpc1145_data *data =
		CONTAINER_OF(cb, struct fpc1145_data, irq_cb);

	fpc1145_disable_irq(data->dev);

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

static int fpc1145_init_driver(const struct device *dev)
{
	const struct fpc1145_cfg *cfg = dev->config;
	struct fpc1145_data *data = dev->data;
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
	gpio_init_callback(&data->irq_cb, fpc1145_irq, BIT(cfg->interrupt.pin));
	gpio_add_callback_dt(&cfg->interrupt, &data->irq_cb);

	k_sem_init(&nocache_buffs_lock, 1, 1);

	return 0;
}

#define FPC1145_SENSOR_INFO(inst)                                         \
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

/* The sensor context is uncached as it contains the SPI buffers, which is
 * needed by DMA. Also, the binary library assumes that it is aligned.
 *
 * Unlike FPC1025 driver, the FPC1145 driver doesn't need to keep CS asserted
 * between transactions, so SPI_HOLD_ON_CS in not needed here.
 */
#define FPC1145_DEFINE(inst)                                                   \
	static uint8_t ctx_##inst[FP_SENSOR_CONTEXT_SIZE] __nocache __aligned( \
		4);                                                            \
	static struct fpc1145_data fpc1145_data_##inst = {                     \
		.ctx = ctx_##inst,                                             \
	};                                                                     \
	static const struct fpc1145_cfg fpc1145_cfg_##inst = {                 \
		.spi = SPI_DT_SPEC_INST_GET(                                   \
			inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0),        \
		.interrupt = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),           \
		.reset_pin = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),         \
		.info = FPC1145_SENSOR_INFO(inst),                             \
	};                                                                     \
	BUILD_ASSERT(                                                          \
		CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE >=                        \
			FINGERPRINT_SENSOR_REAL_IMAGE_SIZE(DT_DRV_INST(inst)), \
		"FP image buffer size is smaller than raw image size");        \
	DEVICE_DT_INST_DEFINE(inst, fpc1145_init_driver, NULL,                 \
			      &fpc1145_data_##inst, &fpc1145_cfg_##inst,       \
			      POST_KERNEL,                                     \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,         \
			      &cros_fp_fpc1145_driver_api)

DT_INST_FOREACH_STATUS_OKAY(FPC1145_DEFINE);
