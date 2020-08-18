/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Controls for the mock fpsensor private driver
 */

#ifndef __MOCK_FP_SENSOR_MOCK_H
#define __MOCK_FP_SENSOR_MOCK_H

#include "common.h"
#include "fpsensor.h"

struct mock_ctrl_fp_sensor {
	int fp_sensor_init_return;
	int fp_sensor_deinit_return;
	int fp_sensor_get_info_return;
	enum finger_state fp_sensor_finger_status_return;
	int fp_sensor_acquire_image_return;
	int fp_sensor_acquire_image_with_mode_return;
	int fp_finger_match_return;
	int fp_enrollment_begin_return;
	int fp_enrollment_finish_return;
	int fp_finger_enroll_return;
	int fp_maintenance_return;
};

#define MOCK_CTRL_DEFAULT_FP_SENSOR                                    \
(struct mock_ctrl_fp_sensor) {                                         \
	.fp_sensor_init_return                       = EC_SUCCESS,     \
	.fp_sensor_deinit_return                     = EC_SUCCESS,     \
	.fp_sensor_get_info_return                   = EC_SUCCESS,     \
	.fp_sensor_finger_status_return              = FINGER_NONE,    \
	.fp_sensor_acquire_image_return              = 0,              \
	.fp_sensor_acquire_image_with_mode_return    = 0,              \
	.fp_finger_match_return    = EC_MKBP_FP_ERR_MATCH_YES_UPDATED, \
	.fp_enrollment_begin_return                  = 0,              \
	.fp_enrollment_finish_return                 = 0,              \
	.fp_finger_enroll_return   = EC_MKBP_FP_ERR_ENROLL_OK,         \
	.fp_maintenance_return			     = EC_SUCCESS      \
}

extern struct mock_ctrl_fp_sensor mock_ctrl_fp_sensor;

#endif /* __MOCK_FP_SENSOR_MOCK_H */
