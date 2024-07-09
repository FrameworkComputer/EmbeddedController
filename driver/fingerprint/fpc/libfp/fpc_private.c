/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "assert.h"
#include "common.h"
#include "console.h"
#include "fpc_bio_algorithm.h"
#include "fpc_libfp_matcher.h"
#include "fpc_libfp_sensor.h"
#include "fpc_private.h"
#include "fpc_sensor.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "gpio.h"
#include "link_defs.h"
#include "spi.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#include <stddef.h>
#include <string.h>

#include <sys/types.h>

/* Minimum reset duration */
#define FP_SENSOR_RESET_DURATION_US (10 * MSEC)
/* Maximum delay for the interrupt to be asserted after the sensor is reset */
#define FP_SENSOR_IRQ_MAX_DELAY_US (5 * MSEC)
/* Maximum number of attempts to initialise the sensor */
#define FP_SENSOR_MAX_INIT_ATTEMPTS 10
/* Delay between failed attempts of fp_sensor_open() */
#define FP_SENSOR_OPEN_DELAY_US (500 * MSEC)

/* Decode internal error codes from FPC's sensor library */
#define FPC_GET_INTERNAL_CODE(res) (((res) & 0x000fc000) >> 14)
/* There was a finger on the sensor when calibrating finger detect */
#define FPC_INTERNAL_FINGER_DFD FPC_ERROR_INTERNAL_38

/*
 * The sensor context is uncached as it contains the SPI buffers,
 * the binary library assumes that it is aligned.
 */
static uint8_t ctx[FP_SENSOR_CONTEXT_SIZE_FPC] __uncached __aligned(4);
static bio_sensor_t bio_sensor;
static uint8_t enroll_ctx[FP_ALGORITHM_ENROLLMENT_SIZE_FPC] __aligned(4);

/* recorded error flags */
static uint16_t errors;

/* Sensor description */
static struct ec_response_fp_info fpc1145_info = {
	/* Sensor identification */
	.vendor_id = FOURCC('F', 'P', 'C', ' '),
	.product_id = 9,
	.model_id = 1,
	.version = 1,
	/* Image frame characteristics */
	.frame_size = FP_SENSOR_IMAGE_SIZE_FPC,
	.pixel_format = V4L2_PIX_FMT_GREY,
	.width = FP_SENSOR_RES_X_FPC,
	.height = FP_SENSOR_RES_Y_FPC,
	.bpp = FP_SENSOR_RES_BPP_FPC,
};

/* Sensor IC commands */
enum fpc_cmd {
	FPC_CMD_STATUS = 0x14,
	FPC_CMD_INT_STS = 0x18,
	FPC_CMD_INT_CLR = 0x1C,
	FPC_CMD_FINGER_QUERY = 0x20,
	FPC_CMD_SLEEP = 0x28,
	FPC_CMD_DEEPSLEEP = 0x2C,
	FPC_CMD_SOFT_RESET = 0xF8,
	FPC_CMD_HW_ID = 0xFC,
};

/* Maximum size of a sensor command SPI transfer */
#define MAX_CMD_SPI_TRANSFER_SIZE 3

/* Uncached memory for the SPI transfer buffer */
static uint8_t spi_buf[MAX_CMD_SPI_TRANSFER_SIZE] __uncached;

static int fpc_send_cmd(const uint8_t cmd)
{
	spi_buf[0] = cmd;
	return spi_transaction(SPI_FP_DEVICE, spi_buf, 1, spi_buf,
			       SPI_READBACK_ALL);
}

void fp_sensor_low_power(void)
{
	/*
	 * TODO(b/117620462): verify that sleep mode is WAI (no increased
	 * latency, expected power consumption).
	 */
	if (0)
		fpc_send_cmd(FPC_CMD_SLEEP);
}

int fpc_get_hwid(uint16_t *id)
{
	int rc;
	uint16_t sensor_id;

	if (id == NULL)
		return EC_ERROR_INVAL;

	spi_buf[0] = FPC_CMD_HW_ID;
	rc = spi_transaction(SPI_FP_DEVICE, spi_buf, 3, spi_buf,
			     SPI_READBACK_ALL);
	if (rc) {
		CPRINTS("FPC HW ID read failed %d", rc);
		return FP_ERROR_SPI_COMM;
	}

	sensor_id = ((spi_buf[1] << 8) | spi_buf[2]);
	*id = sensor_id;

	return EC_SUCCESS;
}

int fpc_check_hwid(void)
{
	uint16_t id = 0;
	int status;

	// TODO(b/361826387): Reconcile the different behavior and handling of
	// the |errors| global state between the libfp and bep implementations.
	/* Clear previous occurrences of relevant |errors| flags. */
	errors &= (~FP_ERROR_SPI_COMM & ~FP_ERROR_BAD_HWID);
	status = fpc_get_hwid(&id);
	assert(status != EC_ERROR_INVAL);
	if (status == FP_ERROR_SPI_COMM)
		errors |= FP_ERROR_SPI_COMM;

	if ((id >> 4) != FP_SENSOR_HWID_FPC) {
		CPRINTS("FPC unknown silicon 0x%04x", id);
		errors |= FP_ERROR_BAD_HWID;
		return EC_ERROR_HW_INTERNAL;
	}
	if (status == EC_SUCCESS)
		CPRINTS(FP_SENSOR_NAME_FPC " id 0x%04x", id);
	return status;
}

static uint8_t fpc_read_clear_int(void)
{
	spi_buf[0] = FPC_CMD_INT_CLR;
	spi_buf[1] = 0xff;
	if (spi_transaction(SPI_FP_DEVICE, spi_buf, 2, spi_buf,
			    SPI_READBACK_ALL))
		return 0xff;
	return spi_buf[1];
}

/*
 * Toggle the h/w reset pins and clear any pending IRQs before initializing the
 * sensor contexts.
 * Returns:
 * - EC_SUCCESS on success.
 * - EC_ERROR_HW_INTERNAL on failure (and |errors| variable is updated where
 *   appropriate).
 */
static int fpc_pulse_hw_reset(void)
{
	int ret;
	int rc = EC_SUCCESS;
	/* Clear previous occurrence of possible error flags. */
	errors &= ~FP_ERROR_NO_IRQ;

	/* Ensure we pulse reset low to initiate the startup */
	gpio_set_level(GPIO_FP_RST_ODL, 0);
	crec_usleep(FP_SENSOR_RESET_DURATION_US);
	gpio_set_level(GPIO_FP_RST_ODL, 1);
	/* the IRQ line should be set high by the sensor */
	crec_usleep(FP_SENSOR_IRQ_MAX_DELAY_US);
	if (!gpio_get_level(GPIO_FPS_INT)) {
		CPRINTS("Sensor IRQ not ready");
		errors |= FP_ERROR_NO_IRQ;
		rc = EC_ERROR_HW_INTERNAL;
	}

	/* Check the Hardware ID */
	ret = fpc_check_hwid();
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to verify HW ID");
		rc = EC_ERROR_HW_INTERNAL;
	}

	/* clear the pending 'ready' IRQ before enabling interrupts */
	fpc_read_clear_int();

	return rc;
}

/* Reset and initialize the sensor IC */
int fp_sensor_init(void)
{
	int res;
	int attempt;

	errors = FP_ERROR_DEAD_PIXELS_UNKNOWN;

	/* Release any previously held resources from earlier iterations */
	res = bio_sensor_destroy(bio_sensor);
	if (res)
		CPRINTS("FPC Sensor resources release failed: %d", res);
	bio_sensor = NULL;

	res = bio_algorithm_exit();
	if (res)
		CPRINTS("FPC Algorithm resources release failed: %d", res);

	/* Print the binary libfpsensor.a library version */
	CPRINTF("FPC libfpsensor.a v%s\n", fp_sensor_get_version());
	cflush();

	attempt = 0;
	do {
		attempt++;

		res = fpc_pulse_hw_reset();
		if (res != EC_SUCCESS) {
			/* In case of failure, retry after a delay. */
			CPRINTS("H/W sensor reset failed, error flags: 0x%x",
				errors);
			cflush();
			crec_usleep(FP_SENSOR_OPEN_DELAY_US);
			continue;
		}

		/*
		 * Ensure that any previous context data is obliterated in case
		 * of a sensor reset.
		 */
		memset(ctx, 0, FP_SENSOR_CONTEXT_SIZE_FPC);

		res = fp_sensor_open(ctx, FP_SENSOR_CONTEXT_SIZE_FPC);
		/* Flush messages from the PAL if any */
		cflush();
		CPRINTS("Sensor init (attempt %d): 0x%x", attempt, res);
		/*
		 * Retry on failure. This typically happens if the user has left
		 * their finger on the sensor after powering up the device, DFD
		 * will fail in that case. We've seen other error modes in the
		 * field, retry in all cases to be more resilient.
		 */
		if (!res)
			break;
		crec_usleep(FP_SENSOR_OPEN_DELAY_US);
	} while (attempt < FP_SENSOR_MAX_INIT_ATTEMPTS);
	if (res)
		errors |= FP_ERROR_INIT_FAIL;

	res = bio_algorithm_init();
	/* the PAL might have spewed a lot of traces, ensure they are visible */
	cflush();
	CPRINTS("Algorithm init: 0x%x", res);
	if (res < 0)
		errors |= FP_ERROR_INIT_FAIL;
	res = bio_sensor_create(&bio_sensor);
	CPRINTS("Sensor create: 0x%x", res);
	if (res < 0)
		errors |= FP_ERROR_INIT_FAIL;

	/* Go back to low power */
	fp_sensor_low_power();

	return EC_SUCCESS;
}

/* Deinitialize the sensor IC */
int fp_sensor_deinit(void)
{
	/*
	 * TODO(tomhughes): libfp doesn't have fp_sensor_close like BEP does.
	 * We'll need FPC to either add it or verify that we don't have the same
	 * problem with the libfp library as described in:
	 * b/124773209#comment46
	 */
	return EC_SUCCESS;
}

int fp_sensor_get_info(struct ec_response_fp_info *resp)
{
	uint16_t sensor_id;

	memcpy(resp, &fpc1145_info, sizeof(*resp));

	if (fpc_get_hwid(&sensor_id))
		return EC_RES_ERROR;

	resp->model_id = sensor_id;
	resp->errors = errors;

	return EC_SUCCESS;
}

__overridable int fp_finger_match(void *templ, uint32_t templ_count,
				  uint8_t *image, int32_t *match_index,
				  uint32_t *update_bitmap)
{
	return bio_template_image_match_list(templ, templ_count, image,
					     match_index, update_bitmap);
}

__overridable int fp_enrollment_begin(void)
{
	int rc;
	bio_enrollment_t p = enroll_ctx;

	rc = bio_enrollment_begin(bio_sensor, &p);
	if (rc < 0)
		CPRINTS("begin failed %d", rc);
	return rc;
}

__overridable int fp_enrollment_finish(void *templ)
{
	bio_template_t pt = templ;

	return bio_enrollment_finish(enroll_ctx, templ ? &pt : NULL);
}

__overridable int fp_finger_enroll(uint8_t *image, int *completion)
{
	int rc = bio_enrollment_add_image(enroll_ctx, image);

	if (rc < 0)
		return rc;
	*completion = bio_enrollment_get_percent_complete(enroll_ctx);
	return rc;
}

int fp_maintenance(void)
{
	return fpc_fp_maintenance(&errors);
}

int fp_acquire_image_with_mode(uint8_t *image_data, int mode)
{
	return fp_sensor_acquire_image_with_mode(image_data, mode);
}

int fp_acquire_image(uint8_t *image_data)
{
	return fp_sensor_acquire_image(image_data);
}

enum finger_state fp_finger_status(void)
{
	return fp_sensor_finger_status();
}

void fp_configure_detect(void)
{
	return fp_sensor_configure_detect();
}
