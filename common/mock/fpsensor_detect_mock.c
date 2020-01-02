/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/fpsensor_detect_mock.h"

struct mock_ctrl_fpsensor_detect mock_ctrl_fpsensor_detect =
	MOCK_CTRL_DEFAULT_FPSENSOR_DETECT;

enum fp_sensor_type get_fp_sensor_type(void)
{
	return mock_ctrl_fpsensor_detect.get_fp_sensor_type_return;
}

enum fp_transport_type get_fp_transport_type(void)
{
	return mock_ctrl_fpsensor_detect.get_fp_transport_type_return;
}
