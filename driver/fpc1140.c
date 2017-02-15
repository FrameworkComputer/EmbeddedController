/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "endian.h"
#include "gpio.h"
#include "mkbp_event.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_FP, format, ## args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ## args)

/* Sensor IC commands */
enum fpc_cmd {
	FPC_CMD_STATUS            = 0x14,
	FPC_CMD_INT_STS           = 0x18,
	FPC_CMD_INT_CLR           = 0x1C,
	FPC_CMD_FINGER_QUERY      = 0x20,
	FPC_CMD_SLEEP             = 0x28,
	FPC_CMD_SOFT_RESET        = 0xF8,
	FPC_CMD_HW_ID             = 0xFC,
};

#define FPC_INT_FINGER_DOWN (1 << 0)

#ifndef SPI_FPC_DEVICE
#define SPI_FPC_DEVICE (&spi_devices[0])
#endif

static uint32_t fp_events;

static int fpc_send_cmd(const uint8_t cmd)
{
	return spi_transaction(SPI_FPC_DEVICE, &cmd, 1, NULL, 0);
}

static int fpc_check_hwid(void)
{
	const uint8_t cmd = FPC_CMD_HW_ID;
	uint16_t id;
	int rc;

	rc = spi_transaction(SPI_FPC_DEVICE, &cmd, 1, (void *)&id, sizeof(id));
	if (rc) {
		CPRINTS("FPC ID read failed %d", rc);
		return rc;
	}
	id = be16toh(id);
	if ((id >> 4) != 0x140) {
		CPRINTS("FPC unknown silicon 0x%04x", id);
		return EC_ERROR_INVAL;
	}
	CPRINTS("FPC1140 id 0x%04x", id);

	return EC_SUCCESS;
}

static uint8_t fpc_read_clear_int(void)
{
	const uint8_t cmd = FPC_CMD_INT_CLR;
	uint8_t val = 0xff;

	if (spi_transaction(SPI_FPC_DEVICE, &cmd, 1, &val, 1))
		return 0xff;
	return val;
}

static uint8_t fpc_read_int(void)
{
	const uint8_t cmd = FPC_CMD_INT_STS;
	uint8_t val = 0xff;

	if (spi_transaction(SPI_FPC_DEVICE, &cmd, 1, &val, 1))
		return 0xff;
	return val;
}

/* Reset and initialize the sensor IC */
static int fpc_init(void)
{
	/* configure the SPI controller (also ensure that CS_N is high) */
	gpio_config_module(MODULE_SPI_MASTER, 1);
	spi_enable(CONFIG_SPI_FP_PORT, 1);

	/* Ensure we pulse reset low to initiate the startup */
	gpio_set_level(GPIO_FP_RST_ODL, 0);
	usleep(100);
	gpio_set_level(GPIO_FP_RST_ODL, 1);
	/* the IRQ line should be set high by the sensor */
	usleep(10000);
	if (!gpio_get_level(GPIO_FPS_INT)) {
		CPRINTS("FPC not ready");
		return EC_ERROR_TIMEOUT;
	}

	/* Check the Hardware ID */
	if (fpc_check_hwid())
		return EC_ERROR_INVAL;

	/* clear the pending 'ready' IRQ before enabling interrupts */
	fpc_read_clear_int();
	gpio_enable_interrupt(GPIO_FPS_INT);

	fpc_send_cmd(FPC_CMD_SLEEP);

	return EC_SUCCESS;
}

/* Interrupt line from the fingerprint sensor */
void fps_event(enum gpio_signal signal)
{
	task_wake(TASK_ID_FPC1140);
}

void fp_task(void)
{
	fpc_init();

	while (1) {
		uint8_t evt;

		task_wait_event(-1);
		evt = fpc_read_int();
		atomic_or(&fp_events, evt);
		CPRINTS("FPS event %02x", evt);

		if (evt & FPC_INT_FINGER_DOWN)
			CPRINTS("Finger!");

		if (evt)
			mkbp_send_event(EC_MKBP_EVENT_FINGERPRINT);
	}
}

static int fp_get_next_event(uint8_t *out)
{
	uint32_t event_out = atomic_read_clear(&fp_events);

	memcpy(out, &event_out, sizeof(event_out));

	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_FINGERPRINT, fp_get_next_event);
