/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fingerprint_ft98xx.h"
#include "fingerprint_ft98xx_pal.h"
#include "fingerprint_ft98xx_private.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/fingerprint.h>

/* Platform Abstraction Layer for FT binary */

LOG_MODULE_REGISTER(ft98xx_pal, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(cros_fp_fingerprint_sensor)
#error "cros-fp,fingerprint-sensor device must be chosen"
#else
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

K_HEAP_DEFINE(fp_driver_heap, CONFIG_FINGERPRINT_SENSOR_FT98XX_HEAP_SIZE);

void ft_delay_ms(uint32_t ms)
{
	k_msleep(ms);
}

int ft_sensor_hw_reset(void)
{
	const struct ft98xx_cfg *cfg = fp_sensor_dev->config;

	gpio_pin_set_dt(&cfg->reset_pin, 1);
	k_msleep(FP_SENSOR_HW_RESET_TIME_MS);
	gpio_pin_set_dt(&cfg->reset_pin, 0);
	return 0;
}

int ft_spi_write(uint8_t *buffer, uint32_t len)
{
	int ret = 0;
	const struct ft98xx_cfg *cfg = fp_sensor_dev->config;

	struct spi_buf tx_buf[] = {
		{ .buf = buffer, .len = len },
	};

	struct spi_buf_set tx_set = { .buffers = tx_buf,
				      .count = ARRAY_SIZE(tx_buf) };

	ret = spi_transceive_dt(&cfg->spi, &tx_set, NULL);

	return ret;
}

int ft_spi_write_then_read(uint8_t *tx_buffer, uint32_t tx_len,
			   uint8_t *rx_buffer, uint32_t rx_len)
{
	int ret;
	const struct ft98xx_cfg *cfg = fp_sensor_dev->config;

	struct spi_buf tx_buf[] = {
		{ .buf = tx_buffer, .len = tx_len },
		{ .buf = NULL, .len = rx_len },
	};

	struct spi_buf rx_buf[] = {
		{ .buf = NULL, .len = tx_len },
		{ .buf = rx_buffer, .len = rx_len },
	};

	struct spi_buf_set tx_set = { .buffers = tx_buf,
				      .count = ARRAY_SIZE(tx_buf) };
	struct spi_buf_set rx_set = { .buffers = rx_buf,
				      .count = ARRAY_SIZE(rx_buf) };

	ret = spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);

	return ret;
}

void *__unused focal_malloc(uint32_t size)
{
	void *p = k_heap_aligned_alloc(&fp_driver_heap, sizeof(void *), size,
				       K_NO_WAIT);

	if (p == NULL) {
		LOG_ERR("Error - %s of size %u failed.", __func__, size);
	}

	return p;
}

void __unused focal_free(void *data)
{
	k_heap_free(&fp_driver_heap, data);
}
