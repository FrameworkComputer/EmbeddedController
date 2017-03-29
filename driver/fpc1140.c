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
#include "host_command.h"
#include "mkbp_event.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#ifdef HAVE_PRIVATE
#include "fpc1145_private.h"

uint8_t fp_buffer[FPC_IMAGE_SIZE];
#endif

#define CPRINTF(format, args...) cprintf(CC_FP, format, ## args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ## args)

/* Sensor IC commands */
enum fpc_cmd {
	FPC_CMD_STATUS            = 0x14,
	FPC_CMD_INT_STS           = 0x18,
	FPC_CMD_INT_CLR           = 0x1C,
	FPC_CMD_FINGER_QUERY      = 0x20,
	FPC_CMD_SLEEP             = 0x28,
	FPC_CMD_DEEPSLEEP         = 0x2B,
	FPC_CMD_SOFT_RESET        = 0xF8,
	FPC_CMD_HW_ID             = 0xFC,
};

#define FPC_IDLE_MASK		0x1E

#define FPC_INT_FINGER_DOWN (1 << 0)

#ifndef SPI_FPC_DEVICE
#define SPI_FPC_DEVICE (&spi_devices[0])
#endif

static uint32_t fp_events;
static uint32_t sensor_mode;

/* Opaque sensor configuration registers settings */
static union {
	struct ec_params_fp_sensor_config sensor_config;
	uint8_t data[0x220];
} u;

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

static uint8_t fpc_read_status(void)
{
	const uint8_t cmd = FPC_CMD_STATUS;
	uint8_t val[2];

	if (spi_transaction(SPI_FPC_DEVICE, &cmd, 1, val, 2))
		return 0xff;
	return val[1];
}

static int fpc_wait_for_idle(void)
{
	uint8_t sts;
	int retries = 100;

	do {
		fpc_read_clear_int();
		sts = fpc_read_status();
	} while (sts != FPC_IDLE_MASK && retries--);

	return sts != FPC_IDLE_MASK;
}

/* Reset and initialize the sensor IC */
static int fpc_init(void)
{
#ifdef HAVE_PRIVATE
	memcpy(&u.sensor_config, &fpc1145_config, fpc1145_config_size);
#endif
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

	fpc_send_cmd(FPC_CMD_DEEPSLEEP);

	return EC_SUCCESS;
}

static void fp_configure_sensor(void)
{
	int i, index;

	for (i = 0, index = 0; i < u.sensor_config.count; i++) {
		uint8_t *data = u.sensor_config.data + index;
		int len = u.sensor_config.len[i];
		int rc = spi_transaction(&spi_devices[0], data, len, NULL, 0);

		if (rc)
			CPRINTS("Config %d failed with %d for 0x%02x",
				i, rc, data[0]);
		index += len;
	}
}

static void fp_prepare_capture(void)
{
	/* wake it from deep-sleep by doing a soft-reset */
	fpc_send_cmd(FPC_CMD_SOFT_RESET);
	fpc_wait_for_idle();
	fp_configure_sensor();
	/* sleep until the finger down is detected */
	fpc_send_cmd(FPC_CMD_SLEEP);
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

static int fp_command_passthru(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_passthru *params = args->params;
	void *out = args->response;
	int rc;
	int ret = EC_RES_SUCCESS;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (params->len > args->params_size +
	    offsetof(struct ec_params_fp_passthru, data) ||
	    params->len > args->response_max)
		return EC_RES_INVALID_PARAM;

	rc = spi_transaction_async(&spi_devices[0], params->data,
				   params->len, out, SPI_READBACK_ALL);
	if (params->flags & EC_FP_FLAG_NOT_COMPLETE)
		rc |= spi_transaction_wait(&spi_devices[0]);
	else
		rc |= spi_transaction_flush(&spi_devices[0]);

	if (rc == EC_ERROR_TIMEOUT)
		ret = EC_RES_TIMEOUT;
	else if (rc)
		ret = EC_RES_ERROR;

	args->response_size = params->len;
	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_PASSTHRU, fp_command_passthru, EC_VER_MASK(0));

static int fp_command_sensor_config(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_sensor_config *p = args->params;
	int i, index;
	unsigned limit = args->params_size
		- offsetof(struct ec_params_fp_sensor_config, data);

	/* Validate the content size */
	if (p->count > EC_FP_SENSOR_CONFIG_MAX_REGS ||
	    args->params_size > sizeof(u))
		return EC_RES_INVALID_PARAM;
	for (i = 0, index = 0; i < p->count; i++) {
		if (index + p->len[i] > limit)
			return EC_RES_INVALID_PARAM;
		index += p->len[i];
	}

	memcpy(&u.sensor_config, p, args->params_size);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_SENSOR_CONFIG, fp_command_sensor_config,
		     EC_VER_MASK(0));

static int fp_command_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_mode *p = args->params;
	struct ec_response_fp_mode *r = args->response;

	if (!(p->mode & FP_MODE_DONT_CHANGE)) {
		sensor_mode = p->mode;
		if (p->mode & FP_MODE_DEEPSLEEP) {
			fpc_send_cmd(FPC_CMD_DEEPSLEEP);
		} else {
			if (p->mode & FP_MODE_FINGER_DOWN)
				fp_prepare_capture();
			if (p->mode & FP_MODE_FINGER_UP)
				/* TBD */;
		}
	}

	r->mode = sensor_mode;
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_MODE, fp_command_mode, EC_VER_MASK(0));

static int fp_command_info(struct host_cmd_handler_args *args)
{
	struct ec_response_fp_info *r = args->response;
	int rc;
	const uint8_t cmd = FPC_CMD_HW_ID;
	uint16_t id;
	int ret = EC_RES_SUCCESS;

#ifdef HAVE_PRIVATE
	memcpy(r, &fpc1145_info, sizeof(*r));
#else
	return EC_RES_UNAVAILABLE;
#endif
	rc = spi_transaction(SPI_FPC_DEVICE, &cmd, 1, (void *)&id, sizeof(id));
	if (rc)
		return EC_RES_ERROR;
	r->model_id = be16toh(id);

	args->response_size = sizeof(*r);
	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_INFO, fp_command_info, EC_VER_MASK(0));

static int fp_command_frame(struct host_cmd_handler_args *args)
{
#ifdef HAVE_PRIVATE
	const struct ec_params_fp_frame *params = args->params;
	void *out = args->response;

	if (params->offset + params->size > sizeof(fp_buffer) ||
	    params->size > args->response_max)
		return EC_RES_INVALID_PARAM;

	memcpy(out, fp_buffer + params->offset, params->size);

	args->response_size = params->size;
	return EC_RES_SUCCESS;
#else
	return EC_RES_UNAVAILABLE;
#endif
}
DECLARE_HOST_COMMAND(EC_CMD_FP_FRAME, fp_command_frame, EC_VER_MASK(0));
