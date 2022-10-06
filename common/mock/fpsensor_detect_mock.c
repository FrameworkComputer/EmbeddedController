/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/fpsensor_detect_mock.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

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

enum fp_sensor_spi_select get_fp_sensor_spi_select(void)
{
	return mock_ctrl_fpsensor_detect.get_fp_sensor_spi_select_return;
}
