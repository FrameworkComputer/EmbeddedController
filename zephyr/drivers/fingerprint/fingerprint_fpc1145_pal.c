/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fingerprint_fpc1145.h"
#include "fingerprint_fpc1145_pal.h"
#include "fingerprint_fpc1145_private.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/fingerprint.h>

/* Platform Abstraction Layer for FPC libfp binary */

LOG_MODULE_REGISTER(fpc1145_pal, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(cros_fp_fingerprint_sensor)
#error "cros-fp,fingerprint-sensor device must be chosen"
#else
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

int __unused fpc_pal_spi_writeread(fpc_device_t device, uint8_t *tx_buffer,
				   uint8_t *rx_buffer, uint32_t size)
{
	const struct fpc1145_cfg *cfg = fp_sensor_dev->config;
	const struct spi_buf tx_buf[1] = { { .buf = tx_buffer, .len = size } };
	const struct spi_buf rx_buf[1] = { { .buf = rx_buffer, .len = size } };
	const struct spi_buf_set tx = { .buffers = tx_buf, .count = 1 };
	const struct spi_buf_set rx = { .buffers = rx_buf, .count = 1 };
	int err = spi_transceive_dt(&cfg->spi, &tx, &rx);

	if (err != 0) {
		LOG_ERR("spi_transceive_dt() failed, result %d", err);
		return err;
	}

	return 0;
}

int __unused fpc_pal_wait_irq(fpc_device_t device, enum fpc_pal_irq irq_type)
{
	/* TODO: b/72360575 */
	return 0; /* just lie about it, libfpsensor prefers... */
}

int __unused fpc_pal_get_time(uint64_t *time_us)
{
	*time_us = k_ticks_to_us_near64(k_uptime_ticks());

	return 0;
}

int __unused fpc_pal_delay_us(uint64_t us)
{
	k_usleep(us);

	return 0;
}

void __unused fpc_pal_log_entry(const char *tag, int log_level,
				const char *format, ...)
{
	va_list args;

	va_start(args, format);
	printk("%s", tag);
	vprintk(format, args);
	va_end(args);
}
