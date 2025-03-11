// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fingerprint_egis630.h"
#include "fingerprint_egis630_pal.h"
#include "fingerprint_egis630_private.h"

#include <stdio.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/cbprintf.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/sys_clock.h>

#include <drivers/fingerprint.h>

/* Platform Abstraction Layer for EGIS binary */

LOG_MODULE_REGISTER(egis630_pal, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(cros_fp_fingerprint_sensor)
#error "cros-fp,fingerprint-sensor device must be chosen"
#else
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

K_HEAP_DEFINE(fp_driver_heap, CONFIG_FINGERPRINT_SENSOR_EGIS630_HEAP_SIZE);
K_SEM_DEFINE(printf_buffer_lock, 1, 1);

IF_DISABLED(CONFIG_ZTEST, (static)) char printf_buffer[256];

int __unused periphery_spi_write_read(uint8_t *write, uint32_t write_len,
				      uint8_t *read, uint32_t read_len)
{
	const struct egis630_cfg *cfg = fp_sensor_dev->config;
	const struct spi_buf tx_buf[] = { { .buf = write, .len = write_len },
					  { .buf = NULL, .len = read_len } };
	const struct spi_buf rx_buf[] = { { .buf = NULL, .len = write_len },
					  { .buf = read, .len = read_len } };
	const struct spi_buf_set tx = { .buffers = tx_buf,
					.count = ARRAY_SIZE(tx_buf) };
	const struct spi_buf_set rx = { .buffers = rx_buf,
					.count = ARRAY_SIZE(rx_buf) };

	int err = spi_transceive_dt(&cfg->spi, &tx, &rx);

	if (err != 0) {
		LOG_ERR("SPI PAL transaction failed: %d", err);
		return -EIO;
	}

	return err;
}

unsigned long long __unused plat_get_time(void)
{
	return k_uptime_get();
}

unsigned long __unused plat_get_diff_time(unsigned long long begin)
{
	unsigned long long nowTime = plat_get_time();

	return (unsigned long)(nowTime - begin);
}

void __unused plat_wait_time(unsigned long msecs)
{
	k_busy_wait(msecs * USEC_PER_MSEC);
	return;
}

void __unused plat_sleep_time(unsigned long timeInMs)
{
	k_msleep(timeInMs);
	return;
}

#ifdef EGIS_DBG
LOG_LEVEL g_log_level = LOG_DEBUG;
#else
LOG_LEVEL g_log_level = LOG_INFO;
#endif

void __unused set_debug_level(LOG_LEVEL level)
{
	g_log_level = level;
	output_log(LOG_ERROR, "RBS", "", "", 0, "set_debug_level %d", level);
}

void __unused output_log(LOG_LEVEL level, const char *tag,
			 const char *file_path, const char *func, int line,
			 const char *format, ...)
{
	if (format == NULL)
		return;
	if (g_log_level > level)
		return;

	k_sem_take(&printf_buffer_lock, K_FOREVER);
	va_list vl;
	va_start(vl, format);
	int n = snprintf(printf_buffer, sizeof(printf_buffer), "<%s:%d> ", func,
			 line);
	n += vsnprintf(printf_buffer + n, sizeof(printf_buffer) - n, format,
		       vl);
	va_end(vl);

	switch (level) {
	case LOG_ERROR:
		LOG_ERR("%s", printf_buffer);
		break;
	case LOG_INFO:
	case LOG_DEBUG:
	case LOG_VERBOSE:
		LOG_INF("%s", printf_buffer);
		break;
	default:
		break;
	}
	k_sem_give(&printf_buffer_lock);
}

void __unused *sys_alloc(size_t count, size_t size)
{
	void *p = k_heap_aligned_alloc(&fp_driver_heap, sizeof(void *), size,
				       K_NO_WAIT);

	if (p == NULL) {
		LOG_ERR("Error - %s of size %u failed.", __func__, size);
		/* TODO(b/423622893): Prevent Runtime OOM Panics in Zephyr
		 * Gwendolin via BUILD_ASSERT on the heap size.
		 */
		k_oops();
		CODE_UNREACHABLE;
	}

	return p;
}

void __unused sys_free(void *data)
{
	k_heap_free(&fp_driver_heap, data);
}
