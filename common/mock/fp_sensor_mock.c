/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Mock fpsensor private driver
 */

#include <stdlib.h>

#include "common.h"
#include "fpsensor.h"
#include "mock/fp_sensor_mock.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_ctrl_fp_sensor mock_ctrl_fp_sensor = MOCK_CTRL_DEFAULT_FP_SENSOR;

int fp_sensor_init_mock(void)
{
	return mock_ctrl_fp_sensor.fp_sensor_init_return;
}

int fp_sensor_deinit_mock(void)
{
	return mock_ctrl_fp_sensor.fp_sensor_deinit_return;
}

int fp_sensor_get_info_mock(struct ec_response_fp_info *resp)
{
	resp->version = 0;
	return mock_ctrl_fp_sensor.fp_sensor_get_info_return;
}

void fp_sensor_low_power_mock(void)
{
}

void fp_sensor_configure_detect_mock(void)
{
}

enum finger_state fp_sensor_finger_status_mock(void)
{
	return mock_ctrl_fp_sensor.fp_sensor_finger_status_return;
}

int fp_sensor_acquire_image_mock(uint8_t *image_data)
{
	return mock_ctrl_fp_sensor.fp_sensor_acquire_image_return;
}

int fp_sensor_acquire_image_with_mode_mock(uint8_t *image_data, int mode)
{
	return mock_ctrl_fp_sensor.fp_sensor_acquire_image_with_mode_return;
}

int fp_finger_match_mock(void *templ, uint32_t templ_count, uint8_t *image,
			 int32_t *match_index, uint32_t *update_bitmap)
{
	return mock_ctrl_fp_sensor.fp_finger_match_return;
}

int fp_enrollment_begin_mock(void)
{
	return mock_ctrl_fp_sensor.fp_enrollment_begin_return;
}

int fp_enrollment_finish_mock(void *templ)
{
	return mock_ctrl_fp_sensor.fp_enrollment_finish_return;
}

int fp_finger_enroll_mock(uint8_t *image, int *completion)
{
	return mock_ctrl_fp_sensor.fp_finger_enroll_return;
}

int fp_maintenance_mock(void)
{
	return mock_ctrl_fp_sensor.fp_maintenance_return;
}

struct fp_sensor_interface fp_driver_mock = {
	.sensor_type = FP_SENSOR_TYPE_UNKNOWN,
	.fp_sensor_init = &fp_sensor_init_mock,
	.fp_sensor_deinit = &fp_sensor_deinit_mock,
	.fp_sensor_get_info = &fp_sensor_get_info_mock,
	.fp_sensor_low_power = &fp_sensor_low_power_mock,
	.fp_sensor_configure_detect_ = &fp_sensor_configure_detect_mock,
	.fp_sensor_finger_status_ = &fp_sensor_finger_status_mock,
	.fp_sensor_acquire_image_with_mode_ =
		&fp_sensor_acquire_image_with_mode_mock,
	.fp_finger_enroll = &fp_finger_enroll_mock,
	.fp_finger_match = &fp_finger_match_mock,
	.fp_enrollment_begin = &fp_enrollment_begin_mock,
	.fp_enrollment_finish = &fp_enrollment_finish_mock,
	.fp_maintenance = &fp_maintenance_mock,
	.algorithm_template_size = 0,
	.encrypted_template_size =
		FP_POSITIVE_MATCH_SALT_BYTES +
		sizeof(struct ec_fp_template_encryption_metadata),
	.res_x = 0,
	.res_y = 0
};
