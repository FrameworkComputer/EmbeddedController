/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fingerprint_egis660.h"
#include "fingerprint_egis660_pal.h"
#include "fingerprint_egis660_private.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/fingerprint.h>

/* Platform Abstraction Layer for EGIS binary */

LOG_MODULE_REGISTER(egis660_pal, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(cros_fp_fingerprint_sensor)
#error "cros-fp,fingerprint-sensor device must be chosen"
#else
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

K_HEAP_DEFINE(fp_driver_heap, CONFIG_FINGERPRINT_SENSOR_EGIS660_HEAP_SIZE);

int __unused egis_sensor_spi_write_read(uint8_t *data, size_t write_size,
					size_t read_size,
					bool leave_cs_asserted)
{
	const struct egis660_cfg *cfg = fp_sensor_dev->config;
	struct spi_buf tx_buf[] = { { .buf = data, .len = write_size },
				    { .buf = NULL, .len = read_size } };
	struct spi_buf rx_buf[] = { { .buf = NULL, .len = write_size },
				    { .buf = data + write_size,
				      .len = read_size } };
	struct spi_buf_set tx = { .buffers = tx_buf,
				  .count = ARRAY_SIZE(tx_buf) };
	struct spi_buf_set rx = { .buffers = rx_buf,
				  .count = ARRAY_SIZE(rx_buf) };

	/* Block communicating with sensor by other threads while a series of
	 * SPI transaction is ongoing, until CS is asserted,
	 */
	fp_sensor_lock(fp_sensor_dev);
	int err = spi_transceive_dt(&cfg->spi, &tx, &rx);

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
		fp_sensor_unlock(fp_sensor_dev);
	}

	if (err != 0) {
		LOG_ERR("spi_transceive_dt() failed, result %d", err);
		return EGIS_BEP_RESULT_IO_ERROR;
	}

	return EGIS_BEP_RESULT_OK;
}

int __unused egis_sensor_spi_get_duplex_mode(void)
{
	return 0;
}

bool __unused egis_sensor_check_irq(void)
{
	const struct egis660_cfg *cfg = fp_sensor_dev->config;
	int ret = gpio_pin_get_dt(&cfg->interrupt);

	if (ret < 0) {
		LOG_ERR("Failed to get FP interrupt pin, status: %d", ret);
		return false;
	}

	return (ret == 1);
}

bool __unused egis_sensor_read_irq(void)
{
	return egis_sensor_check_irq();
}

void __unused egis_sensor_reset(bool state)
{
	const struct egis660_cfg *cfg = fp_sensor_dev->config;
	int ret = gpio_pin_set_dt(&cfg->reset_pin, state ? 1 : 0);

	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
	}
}

uint32_t __unused egis_timebase_get_tick(void)
{
	return k_uptime_get_32();
}

void __unused egis_timebase_delay_ms(uint32_t delay)
{
	/* private library needs accurate delay, usually 1ms */
	k_busy_wait(delay * 1000);
}

void __unused *egis_malloc(uint32_t size)
{
	void *p = k_heap_aligned_alloc(&fp_driver_heap, sizeof(void *), size,
				       K_NO_WAIT);

	return p;
}

void __unused egis_free(void *data)
{
	k_heap_free(&fp_driver_heap, data);
}

void __unused egis_log_var(const char *source, uint8_t level,
			   const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vprintk(format, args);
	va_end(args);
}

/* LCOV_EXCL_START - These functions are required by EGIS library.
 * but they are doing nothing.
 */

void __unused egis_assert_fail(const char *file, uint32_t line,
			       const char *func, const char *expr)
{
	/* If need to debug library, implements this */
}

void __unused egis_sensor_spi_init(uint32_t speed_hz)
{
	/* Keep empty because spi is already initialised at other place */
}

int __unused egis_sensor_wfi(uint16_t timeout_ms, egis_wfi_check_t enter_wfi,
			     bool enter_wfi_mode)
{
	/* Always OK */
	return EGIS_BEP_RESULT_OK;
}

/* LCOV_EXCL_STOP */
