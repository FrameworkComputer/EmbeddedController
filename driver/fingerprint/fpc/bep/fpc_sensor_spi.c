/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FPC Platform Abstraction Layer */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "console.h"
#include "fpsensor.h"
#include "fpc_sensor_spi.h"
#include "gpio.h"
#include "spi.h"
#include "util.h"

#include "driver/fingerprint/fpc/fpc_sensor.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_FP, format, ##args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ##args)

#define SPI_BUF_SIZE (1024)

#define FPC_RESULT_OK (0)
#define FPC_RESULT_IO_ERROR (-8)

static uint8_t spi_buf[SPI_BUF_SIZE] FP_FRAME_SECTION __aligned(4);

int __unused fpc_sensor_spi_write_read(uint8_t *write, uint8_t *read,
				       size_t size, bool leave_cs_asserted)
{
	int rc = 0;

	if (size == FP_SENSOR_REAL_IMAGE_SIZE) {
		rc |= spi_transaction(SPI_FP_DEVICE, write, size, read,
				      SPI_READBACK_ALL);
		spi_transaction_flush(SPI_FP_DEVICE);
	} else if (size <= SPI_BUF_SIZE) {
		memcpy(spi_buf, write, size);
		rc |= spi_transaction_async(SPI_FP_DEVICE, spi_buf, size,
					    spi_buf, SPI_READBACK_ALL);

		/* De-asserting the sensor chip-select will clear the sensor
		 * internal command state. To run multiple sensor transactions
		 * in the same command state (typically image capture), leave
		 * chip-select asserted. Make sure chip-select is de-asserted
		 * when all transactions are finished.
		 */
		if (!leave_cs_asserted)
			spi_transaction_flush(SPI_FP_DEVICE);
		else
			spi_transaction_wait(SPI_FP_DEVICE);

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

bool __unused fpc_sensor_spi_check_irq(void)
{
	return (gpio_get_level(GPIO_FPS_INT) == 1);
}

bool __unused fpc_sensor_spi_read_irq(void)
{
	return (gpio_get_level(GPIO_FPS_INT) == 1);
}

void __unused fpc_sensor_spi_reset(bool state)
{
	gpio_set_level(GPIO_FP_RST_ODL, state ? 0 : 1);
}

void __unused fpc_sensor_spi_init(uint32_t speed_hz)
{
}

int __unused fpc_sensor_wfi(uint16_t timeout_ms, fpc_wfi_check_t enter_wfi,
			    bool enter_wfi_mode)
{
	return FPC_RESULT_OK;
}
