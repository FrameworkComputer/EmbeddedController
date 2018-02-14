/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "fpsensor.h"
#include "gpio.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"
#if defined(HAVE_PRIVATE) && !defined(TEST_BUILD)
#define HAVE_FP_PRIVATE_DRIVER
#define PRIV_HEADER(header) STRINGIFY(header)
#include PRIV_HEADER(FP_SENSOR_PRIVATE)
#else
#define FP_SENSOR_IMAGE_SIZE 0
#define FP_SENSOR_RES_X 0
#define FP_SENSOR_RES_Y 0
#endif

/* Last acquired frame */
static uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE];

#define CPRINTF(format, args...) cprintf(CC_FP, format, ## args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ## args)

/* raw image offset inside the acquired frame */
#ifndef FP_SENSOR_IMAGE_OFFSET
#define FP_SENSOR_IMAGE_OFFSET 0
#endif

/* Events for the FPSENSOR task */
#define TASK_EVENT_SENSOR_IRQ     TASK_EVENT_CUSTOM(1)
#define TASK_EVENT_UPDATE_CONFIG  TASK_EVENT_CUSTOM(2)

#define FP_MODE_ANY_DETECT_FINGER (FP_MODE_FINGER_DOWN | FP_MODE_FINGER_UP | \
				   FP_MODE_CAPTURE)
#define FP_MODE_ANY_WAIT_IRQ      (FP_MODE_FINGER_DOWN | FP_MODE_CAPTURE)

/* Delay between 2 s of the sensor to detect finger removal */
#define FINGER_POLLING_DELAY (100*MSEC)

static uint32_t fp_events;
static uint32_t sensor_mode;

/* Interrupt line from the fingerprint sensor */
void fps_event(enum gpio_signal signal)
{
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_SENSOR_IRQ, 0);
}

static inline int is_test_capture(uint32_t mode)
{
	int capture_type = FP_CAPTURE_TYPE(mode);

	return (mode & FP_MODE_CAPTURE)
		&& (capture_type == FP_CAPTURE_PATTERN0
		    || capture_type == FP_CAPTURE_PATTERN1);
}

static inline int is_raw_capture(uint32_t mode)
{
	int capture_type = FP_CAPTURE_TYPE(mode);

	return (capture_type == FP_CAPTURE_VENDOR_FORMAT
	     || capture_type == FP_CAPTURE_QUALITY_TEST);
}

static void send_mkbp_event(uint32_t event)
{
	atomic_or(&fp_events, event);
	mkbp_send_event(EC_MKBP_EVENT_FINGERPRINT);
}

void fp_task(void)
{
	int timeout_us = -1;

	/* configure the SPI controller (also ensure that CS_N is high) */
	gpio_config_module(MODULE_SPI_MASTER, 1);
	spi_enable(CONFIG_SPI_FP_PORT, 1);

#ifdef HAVE_FP_PRIVATE_DRIVER
	/* Reset and initialize the sensor IC */
	fp_sensor_init();

	while (1) {
		uint32_t evt;
		enum finger_state st = FINGER_NONE;

		/* Wait for a sensor IRQ or a new mode configuration */
		evt = task_wait_event(timeout_us);

		if (evt & TASK_EVENT_UPDATE_CONFIG) {
			gpio_disable_interrupt(GPIO_FPS_INT);
			if (is_test_capture(sensor_mode)) {
				fp_sensor_acquire_image_with_mode(fp_buffer,
					FP_CAPTURE_TYPE(sensor_mode));
				sensor_mode &= ~FP_MODE_CAPTURE;
				send_mkbp_event(EC_MKBP_FP_IMAGE_READY);
				continue;
			} else if (sensor_mode & FP_MODE_ANY_DETECT_FINGER) {
				/* wait for a finger on the sensor */
				fp_sensor_configure_detect();
			}
			if (sensor_mode & FP_MODE_DEEPSLEEP)
				/* Shutdown the sensor */
				fp_sensor_low_power();
			if (sensor_mode & FP_MODE_FINGER_UP)
				/* Poll the sensor to detect finger removal */
				timeout_us = FINGER_POLLING_DELAY;
			else
				timeout_us = -1;
			if (sensor_mode & FP_MODE_ANY_WAIT_IRQ)
				gpio_enable_interrupt(GPIO_FPS_INT);
		} else if (evt & (TASK_EVENT_SENSOR_IRQ | TASK_EVENT_TIMER)) {
			gpio_disable_interrupt(GPIO_FPS_INT);
			if (sensor_mode & FP_MODE_ANY_DETECT_FINGER) {
				st = fp_sensor_finger_status();
				if (st == FINGER_PRESENT &&
				    sensor_mode & FP_MODE_FINGER_DOWN) {
					CPRINTS("Finger!");
					sensor_mode &= ~FP_MODE_FINGER_DOWN;
					send_mkbp_event(EC_MKBP_FP_FINGER_DOWN);
				}
				if (st == FINGER_NONE &&
				    sensor_mode & FP_MODE_FINGER_UP) {
					sensor_mode &= ~FP_MODE_FINGER_UP;
					timeout_us = -1;
					send_mkbp_event(EC_MKBP_FP_FINGER_UP);
				}
			}

			if (st == FINGER_PRESENT &&
			    sensor_mode & FP_MODE_CAPTURE) {
				int res = fp_sensor_acquire_image_with_mode(
						fp_buffer,
						FP_CAPTURE_TYPE(sensor_mode));

				if (!res) {
					sensor_mode &= ~FP_MODE_CAPTURE;
					send_mkbp_event(EC_MKBP_FP_IMAGE_READY);
				}
			}
			if (sensor_mode & FP_MODE_ANY_WAIT_IRQ) {
				fp_sensor_configure_detect();
				gpio_enable_interrupt(GPIO_FPS_INT);
			}
		}
	}
#else /* !HAVE_FP_PRIVATE_DRIVER */
	while (1) {
		uint32_t evt = task_wait_event(timeout_us);

		send_mkbp_event(evt);
	}
#endif /* !HAVE_FP_PRIVATE_DRIVER */
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
	/* const struct ec_params_fp_sensor_config *p = args->params; */

	return EC_RES_UNAVAILABLE;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_SENSOR_CONFIG, fp_command_sensor_config,
		     EC_VER_MASK(0));

static int fp_command_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_mode *p = args->params;
	struct ec_response_fp_mode *r = args->response;

	if (!(p->mode & FP_MODE_DONT_CHANGE)) {
		sensor_mode = p->mode;
		task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG, 0);
	}

	r->mode = sensor_mode;
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_MODE, fp_command_mode, EC_VER_MASK(0));

static int fp_command_info(struct host_cmd_handler_args *args)
{
	struct ec_response_fp_info *r = args->response;

#ifdef HAVE_FP_PRIVATE_DRIVER
	if (fp_sensor_get_info(r) < 0)
#endif
		return EC_RES_UNAVAILABLE;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_INFO, fp_command_info, EC_VER_MASK(0));

static int fp_command_frame(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_frame *params = args->params;
	void *out = args->response;
	uint32_t offset = params->offset;

	if (!is_raw_capture(sensor_mode))
		offset += FP_SENSOR_IMAGE_OFFSET;

	if (offset + params->size > sizeof(fp_buffer) ||
	    params->size > args->response_max)
		return EC_RES_INVALID_PARAM;

	memcpy(out, fp_buffer + offset, params->size);

	args->response_size = params->size;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_FRAME, fp_command_frame, EC_VER_MASK(0));

#ifdef CONFIG_CMD_FPSENSOR_DEBUG
/* --- Debug console commands --- */

/*
 * Send the current Fingerprint buffer to the host
 * it is formatted as an 8-bpp PGM ASCII file.
 *
 * In addition, it prepends a short Z-Modem download signature,
 * which triggers automatically your preferred viewer if you configure it
 * properly in "File transfer protocols" in the Minicom options menu.
 * (as triggered by Ctrl-A O)
 * +--------------------------------------------------------------------------+
 * |     Name             Program             Name U/D FullScr IO-Red. Multi  |
 * | A  zmodem     /usr/bin/sz -vv -b          Y    U    N       Y       Y    |
 *  [...]
 * | L  pgm        /usr/bin/display_pgm        N    D    N       Y       N    |
 * | M  Zmodem download string activates... L                                 |
 *
 * My /usr/bin/display_pgm looks like this:
 * #!/bin/sh
 * TMPF=$(mktemp)
 * ascii-xfr -rdv ${TMPF}
 * display ${TMPF}
 */
static void upload_pgm_image(uint8_t *frame)
{
	int x, y;
	uint8_t *ptr = frame;

	/* fake Z-modem ZRQINIT signature */
	ccprintf("#IGNORE for ZModem\r**\030B00");
	msleep(100); /* let the download program start */
	/* Print 8-bpp PGM ASCII header */
	ccprintf("P2\n%d %d\n255\n", FP_SENSOR_RES_X, FP_SENSOR_RES_Y);

	for (y = 0; y < FP_SENSOR_RES_Y; y++) {
		watchdog_reload();
		for (x = 0; x < FP_SENSOR_RES_X; x++, ptr++)
			ccprintf("%d ", *ptr);
		ccputs("\n");
		cflush();
	}

	ccprintf("\x04"); /* End Of Transmission */
}

int command_fptest(int argc, char **argv)
{
	int tries = 200;
	int capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	if (argc >= 2) {
		char *e;

		capture_type = strtoi(argv[1], &e, 0);
		if (*e || capture_type < 0 || capture_type > 3)
			return EC_ERROR_PARAM1;
	}

	ccprintf("Waiting for finger ...\n");
	sensor_mode = FP_MODE_CAPTURE |
		(capture_type << FP_MODE_CAPTURE_TYPE_SHIFT);
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG, 0);

	while (tries--) {
		if (!(sensor_mode & FP_MODE_CAPTURE)) {
			ccprintf("done\n");
			upload_pgm_image(fp_buffer + FP_SENSOR_IMAGE_OFFSET);
			return 0;
		}
		usleep(100 * MSEC);
	}

	return EC_ERROR_TIMEOUT;
}
DECLARE_CONSOLE_COMMAND(fptest, command_fptest, "", "");

#endif /* CONFIG_CMD_FPSENSOR_DEBUG */
