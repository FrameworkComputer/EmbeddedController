/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fpc_bep_matcher.h"
#include "fpc_bep_sensor.h"
#include "fpc_bio_algorithm.h"
#include "fpc_sensor.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "gpio.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "util.h"

#include <stddef.h>
#include <stdint.h>

static uint8_t
	enroll_ctx[FP_ALGORITHM_ENROLLMENT_SIZE_FPC] __aligned(4) = { 0 };

/* Recorded error flags */
static uint16_t errors;

/* Lock to access the sensor */
static K_MUTEX_DEFINE(sensor_lock);
static task_id_t sensor_owner;

/* FPC specific initialization and de-initialization functions */
__staticlib int fp_sensor_open(void);
__staticlib int fp_sensor_close(void);

/* Get FPC library version code.*/
__staticlib const char *fp_sensor_get_version(void);

/* Get FPC library build info.*/
__staticlib const char *fp_sensor_get_build_info(void);

/* Sensor description */
static struct ec_response_fp_info ec_fp_sensor_info = {
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

typedef struct fpc_bep_sensor fpc_bep_sensor_t;

typedef struct {
	const fpc_bep_sensor_t *sensor;
	uint32_t image_buffer_size;
} fpc_sensor_info_t;

#if defined(CONFIG_FP_SENSOR_FPC1025)

__staticlib const fpc_bep_sensor_t fpc_bep_sensor_1025;
__staticlib const fpc_bep_algorithm_t fpc_bep_algorithm_pfe_1025;

__staticlib_hook const fpc_sensor_info_t fpc_sensor_info = {
	.sensor = &fpc_bep_sensor_1025,
	.image_buffer_size = FP_SENSOR_IMAGE_SIZE_FPC,
};

__staticlib_hook const fpc_bio_info_t fpc_bio_info = {
	.algorithm = &fpc_bep_algorithm_pfe_1025,
	.template_size = FP_ALGORITHM_TEMPLATE_SIZE_FPC,
};

#elif defined(CONFIG_FP_SENSOR_FPC1035)

__staticlib const fpc_bep_sensor_t fpc_bep_sensor_1035;
__staticlib const fpc_bep_algorithm_t fpc_bep_algorithm_pfe_1035;

__staticlib_hook const fpc_sensor_info_t fpc_sensor_info = {
	.sensor = &fpc_bep_sensor_1035,
	.image_buffer_size = FP_SENSOR_IMAGE_SIZE_FPC,
};

__staticlib_hook const fpc_bio_info_t fpc_bio_info = {
	.algorithm = &fpc_bep_algorithm_pfe_1035,
	.template_size = FP_ALGORITHM_TEMPLATE_SIZE_FPC,
};
#else
#error "Sensor type not defined!"
#endif

/* Sensor IC commands */
enum fpc_cmd {
	FPC_CMD_DEEPSLEEP = 0x2C,
	FPC_CMD_HW_ID = 0xFC,
};

/* Maximum size of a sensor command SPI transfer */
#define MAX_CMD_SPI_TRANSFER_SIZE 3

/* Memory for the SPI transfer buffer */
static uint8_t spi_buf[MAX_CMD_SPI_TRANSFER_SIZE];

void fp_sensor_lock(void)
{
	if (sensor_owner != task_get_current()) {
		mutex_lock(&sensor_lock);
		sensor_owner = task_get_current();
	}
}

void fp_sensor_unlock(void)
{
	sensor_owner = 0xFF;
	mutex_unlock(&sensor_lock);
}

static int fpc_send_cmd(const uint8_t cmd)
{
	int ret;

	spi_buf[0] = cmd;

	fp_sensor_lock();
	ret = spi_transaction(SPI_FP_DEVICE, spi_buf, 1, spi_buf,
			      SPI_READBACK_ALL);
	fp_sensor_unlock();

	return ret;
}

void fp_sensor_low_power(void)
{
	fpc_send_cmd(FPC_CMD_DEEPSLEEP);
}

int fpc_get_hwid(uint16_t *id)
{
	int rc;
	uint16_t sensor_id;

	if (id == NULL)
		return EC_ERROR_INVAL;

	spi_buf[0] = FPC_CMD_HW_ID;

	fp_sensor_lock();
	rc = spi_transaction(SPI_FP_DEVICE, spi_buf, 3, spi_buf,
			     SPI_READBACK_ALL);
	fp_sensor_unlock();

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

	status = fpc_get_hwid(&id);
	if ((id >> 4) != FP_SENSOR_HWID_FPC) {
		CPRINTS("FPC unknown silicon 0x%04x", id);
		return FP_ERROR_BAD_HWID;
	}
	if (status == EC_SUCCESS)
		CPRINTS(FP_SENSOR_NAME_FPC " id 0x%04x", id);

	return status;
}

/* Reset and initialize the sensor IC */
int fp_sensor_init(void)
{
	int rc;

	/* Print the binary libfpbep.a library version */
	CPRINTS("FPC libfpbep.a %s", fp_sensor_get_version());

	/* Print the BEP version and build time of the library */
	CPRINTS("Build information - %s", fp_sensor_get_build_info());

	errors = FP_ERROR_DEAD_PIXELS_UNKNOWN;

	rc = fp_sensor_open();
	if (rc) {
		errors |= FP_ERROR_INIT_FAIL;
		CPRINTS("Error: fp_sensor_open() failed, result=%d", rc);
	}

	errors |= fpc_check_hwid();

	rc = bio_algorithm_init();
	if (rc < 0) {
		errors |= FP_ERROR_INIT_FAIL;
		CPRINTS("Error: bio_algorithm_init() failed, result=%d", rc);
	}

	/* Go back to low power */
	fp_sensor_low_power();

	return EC_SUCCESS;
}

/* Deinitialize the sensor IC */
int fp_sensor_deinit(void)
{
	int rc;

	rc = bio_algorithm_exit();
	if (rc < 0)
		CPRINTS("Error: bio_algorithm_exit() failed, result=%d", rc);

	rc = fp_sensor_close();
	if (rc < 0)
		CPRINTS("Error: fp_sensor_close() failed, result=%d", rc);

	return rc;
}

int fp_sensor_get_info(struct ec_response_fp_info *resp)
{
	uint16_t sensor_id;

	memcpy(resp, &ec_fp_sensor_info, sizeof(struct ec_response_fp_info));

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
	int rc;

	rc = bio_template_image_match_list(templ, templ_count, image,
					   match_index, update_bitmap);
	if (rc < 0)
		CPRINTS("Error: bio_template_image_match_list() failed, result=%d",
			rc);

	return rc;
}

__overridable int fp_enrollment_begin(void)
{
	int rc;
	bio_enrollment_t bio_enroll = enroll_ctx;

	rc = bio_enrollment_begin(&bio_enroll);
	if (rc < 0)
		CPRINTS("Error: bio_enrollment_begin() failed, result=%d", rc);

	return rc;
}

__overridable int fp_enrollment_finish(void *templ)
{
	int rc;
	bio_enrollment_t bio_enroll = enroll_ctx;
	bio_template_t bio_templ = templ;

	rc = bio_enrollment_finish(bio_enroll, templ ? &bio_templ : NULL);
	if (rc < 0)
		CPRINTS("Error: bio_enrollment_finish() failed, result=%d", rc);

	return rc;
}

__overridable int fp_finger_enroll(uint8_t *image, int *completion)
{
	int rc;
	bio_enrollment_t bio_enroll = enroll_ctx;

	rc = bio_enrollment_add_image(bio_enroll, image);
	if (rc < 0) {
		CPRINTS("Error: bio_enrollment_add_image() failed, result=%d",
			rc);
		return rc;
	}

	*completion = bio_enrollment_get_percent_complete(bio_enroll);

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
