/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* FPC Platform Abstraction Layer callbacks */

#include "common.h"
#include "console.h"
#include "fpc_sensor_pal.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_utils.h"
#include "shared_mem.h"
#include "spi.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

void fpc_pal_log_entry(const char *tag, int log_level, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	uart_puts(tag);
	uart_vprintf(format, args);
	va_end(args);
}

int fpc_pal_delay_us(uint64_t us)
{
	if (us > 250)
		usleep(us);
	else
		udelay(us);
	return 0;
}

int fpc_pal_spi_writeread(fpc_device_t device, uint8_t *tx_buf, uint8_t *rx_buf,
			  uint32_t size)
{
	return spi_transaction(SPI_FP_DEVICE, tx_buf, size, rx_buf,
			       SPI_READBACK_ALL);
}

int fpc_pal_wait_irq(fpc_device_t device, fpc_pal_irq_t irq_type)
{
	/* TODO: b/72360575 */
	return EC_SUCCESS; /* just lie about it, libfpsensor prefers... */
}

int32_t FpcMalloc(void **data, size_t size)
{
	return shared_mem_acquire(size, (char **)data);
}

void FpcFree(void **data)
{
	shared_mem_release(*data);
	*data = NULL;
}
