/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FPC Platform Abstraction Layer */

#include "common.h"
#include "console.h"
#include "driver/fingerprint/fpc/fpc_sensor.h"
#include "fpc_private.h"
#include "fpc_sensor_spi.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_utils.h"
#include "gpio.h"
#include "spi.h"
#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPI_BUF_SIZE (1024)

#define FPC_RESULT_OK (0)
#define FPC_RESULT_IO_ERROR (-8)

static uint8_t spi_buf[SPI_BUF_SIZE] FP_FRAME_SECTION __aligned(4);

__staticlib_hook int fpc_sensor_spi_write_read(uint8_t *write, uint8_t *read,
					       size_t size,
					       bool leave_cs_asserted)
{
	int rc = 0;

	if (size == FP_SENSOR_REAL_IMAGE_SIZE_FPC) {
		fp_sensor_lock();
		rc |= spi_transaction(SPI_FP_DEVICE, write, size, read,
				      SPI_READBACK_ALL);
		spi_transaction_flush(SPI_FP_DEVICE);
		fp_sensor_unlock();
	} else if (size <= SPI_BUF_SIZE) {
		memcpy(spi_buf, write, size);
		fp_sensor_lock();
		rc |= spi_transaction_async(SPI_FP_DEVICE, spi_buf, size,
					    spi_buf, SPI_READBACK_ALL);

		/* De-asserting the sensor chip-select will clear the sensor
		 * internal command state. To run multiple sensor transactions
		 * in the same command state (typically image capture), leave
		 * chip-select asserted. Make sure chip-select is de-asserted
		 * when all transactions are finished.
		 */
		if (!leave_cs_asserted) {
			spi_transaction_flush(SPI_FP_DEVICE);
			fp_sensor_unlock();
		} else {
			spi_transaction_wait(SPI_FP_DEVICE);
		}

		memcpy(read, spi_buf, size);
	} else {
		rc = -1;
	}

	if (rc == 0) {
		return FPC_RESULT_OK;
	} else {
		CPRINTS("Error: spi_transaction()/spi_transaction_async() failed, result=%d",
			rc);
		return FPC_RESULT_IO_ERROR;
	}
}

__staticlib_hook bool fpc_sensor_spi_check_irq(void)
{
	return (gpio_get_level(GPIO_FPS_INT) == 1);
}

__staticlib_hook bool fpc_sensor_spi_read_irq(void)
{
	return (gpio_get_level(GPIO_FPS_INT) == 1);
}

__staticlib_hook void fpc_sensor_spi_reset(bool state)
{
	gpio_set_level(GPIO_FP_RST_ODL, state ? 0 : 1);
}

__staticlib_hook void fpc_sensor_spi_init(uint32_t speed_hz)
{
}

__staticlib_hook int fpc_sensor_wfi(uint16_t timeout_ms,
				    fpc_wfi_check_t enter_wfi,
				    bool enter_wfi_mode)
{
	return FPC_RESULT_OK;
}
