/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/fingerprint.h>
#include <fingerprint/fingerprint_alg.h>
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_detect.h>
#include <fpsensor/fpsensor_state_driver.h>
#include <fpsensor_driver.h>
#include <task.h>

#if !DT_HAS_CHOSEN(cros_fp_fingerprint_sensor)
#error "cros-fp,fingerprint-sensor device must be chosen"
#else
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif
#define CROS_FP_HAS_COMPAT(compat) \
	DT_NODE_HAS_COMPAT(DT_CHOSEN(cros_fp_fingerprint_sensor), compat)

static const struct fingerprint_algorithm *fp_algorithm;

enum fp_sensor_type fpsensor_detect_get_type(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(fp_sensor_sel))
	enum fp_sensor_type ret = FP_SENSOR_TYPE_UNKNOWN;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(div_highside), 1);
	k_usleep(1);
	switch (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(fp_sensor_sel))) {
	case 0:
		ret = FP_SENSOR_TYPE_ELAN;
		break;
	case 1:
		ret = FP_SENSOR_TYPE_FPC;
		break;
	}
	/* We leave GPIO_DIVIDER_HIGHSIDE enabled, since the dragonclaw
	 * development board use it to enable the AND gate (U10) to CS.
	 * Production boards could disable this to save power since it's
	 * only needed for initial detection on those boards.
	 */
	return ret;
#elif CROS_FP_HAS_COMPAT(fpc_fpc1025) || CROS_FP_HAS_COMPAT(fpc_fpc1145)
	return FP_SENSOR_TYPE_FPC;
#elif CROS_FP_HAS_COMPAT(elan_elan80sg) || CROS_FP_HAS_COMPAT(elan_elani80sa)
	return FP_SENSOR_TYPE_ELAN;
#elif CROS_FP_HAS_COMPAT(egis_egis630) || CROS_FP_HAS_COMPAT(egis_egis660)
	return FP_SENSOR_TYPE_EGIS;
#elif CROS_FP_HAS_COMPAT(focaltech_ft98xx)
	return FP_SENSOR_TYPE_FOCALTECH;
#else
#error "Unsupported sensor type"
#endif
}

static void fp_sensor_irq(const struct device *dev)
{
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_SENSOR_IRQ);
}

int fp_sensor_init(void)
{
	int rc;

	rc = fingerprint_init(fp_sensor_dev);
	if (rc) {
		return rc;
	}

	rc = fingerprint_algorithm_count_get();
	if (rc < 1) {
		return -ENOENT;
	}

	/* Get the first available algorithm for now */
	fp_algorithm = fingerprint_algorithm_get(0);
	if (fp_algorithm == NULL) {
		return -ENOENT;
	}

	rc = fingerprint_algorithm_init(fp_algorithm);
	if (rc) {
		return rc;
	}

	rc = fingerprint_config(fp_sensor_dev, fp_sensor_irq);
	if (rc) {
		return rc;
	}

	return 0;
}

int fp_sensor_deinit(void)
{
	int rc;

	rc = fingerprint_algorithm_exit(fp_algorithm);
	if (rc) {
		return rc;
	}

	rc = fingerprint_deinit(fp_sensor_dev);
	if (rc) {
		return rc;
	}

	return 0;
}

int fp_sensor_get_info(struct ec_response_fp_info_v3 *resp, size_t resp_size)
{
	if (resp == NULL) {
		return -EINVAL;
	}

	const size_t expected_min_size =
		sizeof(struct ec_response_fp_info_v3) +
		NUM_IMAGE_CAPTURE_TYPES *
			sizeof(struct fingerprint_image_frame_params);

	if (resp_size < expected_min_size) {
		return -EOVERFLOW;
	}

	// Zero-initialize in case fingerprint_get_info doesn't fill all fields.
	struct fingerprint_sensor_info sensor_info = { 0 };
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES] = { 0 };
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	int rc = fingerprint_get_info(fp_sensor_dev, &sensor_info,
				      image_frame_params_array, &num_params);
	if (rc) {
		return rc;
	}

	if (sensor_info.num_capture_types < 0 ||
	    sensor_info.num_capture_types > NUM_IMAGE_CAPTURE_TYPES) {
		return -EINVAL;
	}

	BUILD_ASSERT(sizeof(resp->sensor_info) == sizeof(sensor_info),
		     "struct fingerprint_sensor_info size mismatch");

	memcpy(&resp->sensor_info, &sensor_info,
	       sizeof(struct fingerprint_sensor_info));

	size_t copy_size = sensor_info.num_capture_types *
			   sizeof(struct fingerprint_image_frame_params);
	memcpy(&resp->image_frame_params, &image_frame_params_array, copy_size);

	return 0;
}

void fp_configure_detect(void)
{
	fingerprint_set_mode(fp_sensor_dev, FINGERPRINT_SENSOR_MODE_DETECT);
}

int fp_acquire_image(uint8_t *image_data, enum fp_capture_type capture_type)
{
	return fingerprint_acquire_image(
		fp_sensor_dev, (enum fingerprint_capture_type)capture_type,
		image_data, FP_SENSOR_IMAGE_SIZE);
}

/* BUILD_ASSERTs to ensure enum values are the same. */
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT ==
	     (int)FP_CAPTURE_VENDOR_FORMAT);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_DEFECT_PXL_TEST ==
	     (int)FP_CAPTURE_DEFECT_PXL_TEST);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_ABNORMAL_TEST ==
	     (int)FP_CAPTURE_ABNORMAL_TEST);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_NOISE_TEST ==
	     (int)FP_CAPTURE_NOISE_TEST);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE ==
	     (int)FP_CAPTURE_SIMPLE_IMAGE);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_PATTERN0 ==
	     (int)FP_CAPTURE_PATTERN0);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_PATTERN1 ==
	     (int)FP_CAPTURE_PATTERN1);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST ==
	     (int)FP_CAPTURE_QUALITY_TEST);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_RESET_TEST ==
	     (int)FP_CAPTURE_RESET_TEST);
BUILD_ASSERT((int)FINGERPRINT_CAPTURE_TYPE_MAX == (int)FP_CAPTURE_TYPE_MAX);

enum finger_state fp_finger_status(void)
{
	int rc;

	rc = fingerprint_finger_status(fp_sensor_dev);
	if (rc < 0) {
		return FINGER_NONE;
	}

	return rc;
}

int fp_enrollment_begin(void)
{
	return fingerprint_enroll_start(fp_algorithm);
}

int fp_finger_enroll(uint8_t *image, int *completion)
{
	return fingerprint_enroll_step(fp_algorithm, image, completion);
}

int fp_enrollment_finish(void *templ)
{
	return fingerprint_enroll_finish(fp_algorithm, templ);
}

int fp_finger_match(void *templ, uint32_t templ_count, uint8_t *image,
		    bool template_update, int32_t *match_index,
		    uint32_t *update_bitmap)
{
	return fingerprint_match(fp_algorithm, templ, templ_count, image,
				 template_update, match_index, update_bitmap);
}

void fp_sensor_low_power(void)
{
	fingerprint_set_mode(fp_sensor_dev, FINGERPRINT_SENSOR_MODE_LOW_POWER);
}

int fp_maintenance(void)
{
	return fingerprint_maintenance(fp_sensor_dev, fp_buffer,
				       sizeof(fp_buffer));
}

int fp_idle(void)
{
	return fingerprint_set_mode(fp_sensor_dev,
				    FINGERPRINT_SENSOR_MODE_IDLE);
}
