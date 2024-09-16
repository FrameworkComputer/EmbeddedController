/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "egis_api.h"
#include "fpsensor/fpsensor.h"
#include "system.h"
#include "task.h"
#include "util.h"

#include <stddef.h>
#include <stdint.h>
#define LOG_TAG "RBS-rapwer"

/* Lock to access the sensor */
static K_MUTEX_DEFINE(sensor_lock);
static task_id_t sensor_owner;

/* Sensor description */
static struct ec_response_fp_info egis_fp_sensor_info = {
	/* Sensor identification */
	.vendor_id = FOURCC('E', 'G', 'I', 'S'),
	.product_id = 9,
	.model_id = 1,
	.version = 1,
	/* Image frame characteristics */
	.frame_size = FP_SENSOR_IMAGE_SIZE_EGIS,
	.pixel_format = V4L2_PIX_FMT_GREY,
	.width = FP_SENSOR_RES_X_EGIS,
	.height = FP_SENSOR_RES_Y_EGIS,
	.bpp = 16,
};

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

void fp_sensor_low_power(void)
{
	egis_sensor_power_down();
}

/* Reset and initialize the sensor IC */
int fp_sensor_init(void)
{
	return egis_sensor_init();
}

/* Deinitialize the sensor IC */
int fp_sensor_deinit(void)
{
	return egis_sensor_deinit();
}

int fp_sensor_get_info(struct ec_response_fp_info *resp)
{
	int rc = EC_SUCCESS;
	egislog_i("");
	memcpy(resp, &egis_fp_sensor_info, sizeof(struct ec_response_fp_info));

	return rc;
}

__overridable int fp_finger_match(void *templ, uint32_t templ_count,
				  uint8_t *image, int32_t *match_index,
				  uint32_t *update_bitmap)
{
	int rc = EC_SUCCESS;
	egislog_i("");

	return rc;
}

__overridable int fp_enrollment_begin(void)
{
	int rc = EC_SUCCESS;
	egislog_i("");
	return rc;
}

__overridable int fp_enrollment_finish(void *templ)
{
	int rc = EC_SUCCESS;
	egislog_i("");

	return rc;
}

__overridable int fp_finger_enroll(uint8_t *image, int *completion)
{
	int rc = EC_SUCCESS;
	egislog_i("");

	return rc;
}

int fp_maintenance(void)
{
	egislog_i("");
	return EC_SUCCESS;
}

int fp_acquire_image_with_mode(uint8_t *image_data, int mode)
{
	int ret = EGIS_API_OK;
	ret = egis_get_image_with_mode(image_data, mode);
	egislog_i("ret %d", ret);

	return EC_SUCCESS;
}

int fp_acquire_image(uint8_t *image_data)
{
	int ret = EGIS_API_OK;
	ret = egis_get_image(image_data);
	egislog_i("ret %d", ret);
	return ret;
}

enum finger_state fp_finger_status(void)
{
	int rc = EGIS_API_FINGER_UNSTABLE;
	egislog_i("");
	rc = egis_check_int_status();

	switch (rc) {
	case EGIS_API_FINGER_PRESENT:
		rc = FINGER_PRESENT;
		break;
	case EGIS_API_FINGER_LOST:
		rc = FINGER_PARTIAL;
		break;
	default:
		rc = FINGER_NONE;
		break;
	}
	return rc;
}

void fp_configure_detect(void)
{
	egislog_i("");
	egis_set_detect_mode();
}
