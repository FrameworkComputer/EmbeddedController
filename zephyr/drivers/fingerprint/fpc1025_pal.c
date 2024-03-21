/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc1025.h"
#include "fpc1025_pal.h"
#include "fpc1025_private.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/fingerprint.h>

/* Platform Abstraction Layer for FPC binary */

LOG_MODULE_REGISTER(fpc1025_pal, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(cros_fp_fingerprint_sensor)
#error "cros-fp,fingerprint-sensor device must be chosen"
#else
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

K_HEAP_DEFINE(fp_driver_heap, CONFIG_FINGERPRINT_SENSOR_FPC1025_HEAP_SIZE);

int __unused fpc_sensor_spi_write_read(uint8_t *write, uint8_t *read,
				       size_t size, bool leave_cs_asserted)
{
	const struct fpc1025_cfg *cfg = fp_sensor_dev->config;
	struct fpc1025_data *data = fp_sensor_dev->data;
	const struct spi_buf tx_buf[1] = { { .buf = write, .len = size } };
	const struct spi_buf rx_buf[1] = { { .buf = read, .len = size } };
	const struct spi_buf_set tx = { .buffers = tx_buf, .count = 1 };
	const struct spi_buf_set rx = { .buffers = rx_buf, .count = 1 };

	/* Block communicating with sensor by other threads while a series of
	 * SPI transaction is ongoing, until CS is asserted,
	 */
	if (!((k_sem_count_get(&data->sensor_lock) == 0) &&
	      (data->sensor_owner == k_current_get()))) {
		k_sem_take(&data->sensor_lock, K_FOREVER);
		data->sensor_owner = k_current_get();
	}

	int err = spi_transceive_dt(&cfg->spi, &tx, &rx);

	/*
	 * EC implementation of this function is always deasserting CS pin when
	 * size == FP_SENSOR_REAL_IMAGE_SIZE. Not sure if it's a bug, but
	 * for now let's introduce assert to check if library can ask to keep
	 * CS asserted when size is real image size.
	 */
	if (leave_cs_asserted &&
	    size == FINGERPRINT_SENSOR_REAL_IMAGE_SIZE(
			    DT_CHOSEN(cros_fp_fingerprint_sensor))) {
		LOG_WRN("FPC library asked to keep CS asserted when size of "
			"the buffer is FP_SENSOR_REAL_IMAGE_SIZE");
	}

	/*
	 * De-asserting the sensor chip-select will clear the sensor
	 * internal command state. To run multiple sensor transactions
	 * in the same command state (typically image capture), leave
	 * chip-select asserted. Make sure chip-select is de-asserted
	 * when all transactions are finished.
	 */
	if (!leave_cs_asserted) {
		/* Release CS line */
		spi_release_dt(&cfg->spi);
		/* Release lock of the sensor */
		data->sensor_owner = NULL;
		k_sem_give(&data->sensor_lock);
	}

	if (err != 0) {
		LOG_ERR("spi_transceive_dt() failed, result %d", err);
		return FPC_BEP_RESULT_IO_ERROR;
	}

	return FPC_BEP_RESULT_OK;
}

bool __unused fpc_sensor_spi_check_irq(void)
{
	const struct fpc1025_cfg *cfg = fp_sensor_dev->config;
	int ret = gpio_pin_get_dt(&cfg->interrupt);

	if (ret < 0) {
		LOG_ERR("Failed to get FP interrupt pin, status: %d", ret);
		return false;
	}

	return (ret == 1);
}

bool __unused fpc_sensor_spi_read_irq(void)
{
	return fpc_sensor_spi_check_irq();
}

void __unused fpc_sensor_spi_reset(bool state)
{
	const struct fpc1025_cfg *cfg = fp_sensor_dev->config;
	int ret = gpio_pin_set_dt(&cfg->reset_pin, state ? 1 : 0);

	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
	}
}

uint32_t __unused fpc_timebase_get_tick(void)
{
	return k_uptime_get_32();
}

void __unused fpc_timebase_busy_wait(uint32_t ms)
{
	k_busy_wait(ms * 1000);
}

void __unused *fpc_malloc(uint32_t size)
{
	void *p = k_heap_aligned_alloc(&fp_driver_heap, sizeof(void *), size,
				       K_NO_WAIT);

	if (p == NULL) {
		LOG_ERR("Failed to allocate %d bytes", size);
		k_oops();
		CODE_UNREACHABLE;
	}

	return p;
}

void __unused fpc_free(void *data)
{
	k_heap_free(&fp_driver_heap, data);
}

void __unused fpc_log_var(const char *source, uint8_t level, const char *format,
			  ...)
{
	va_list args;

	va_start(args, format);
	vprintk(format, args);
	va_end(args);
}

/* LCOV_EXCL_START - These functions are required by FPC library.
 * but they are doing nothing.
 */

void __unused fpc_assert_fail(const char *file, uint32_t line, const char *func,
			      const char *expr)
{
}

void __unused fpc_sensor_spi_init(uint32_t speed_hz)
{
}

int __unused fpc_sensor_wfi(uint16_t timeout_ms, fpc_wfi_check_t enter_wfi,
			    bool enter_wfi_mode)
{
	return FPC_BEP_RESULT_OK;
}

/* LCOV_EXCL_STOP */
