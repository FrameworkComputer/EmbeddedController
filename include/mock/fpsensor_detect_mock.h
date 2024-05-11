/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_FPSENSOR_DETECT_MOCK_H
#define __MOCK_FPSENSOR_DETECT_MOCK_H

#include "fpsensor/fpsensor_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mock_ctrl_fpsensor_detect {
	enum fp_sensor_type fpsensor_detect_get_type_return;
	enum fp_transport_type get_fp_transport_type_return;
	enum fp_sensor_spi_select get_fp_sensor_spi_select_return;
};

#define MOCK_CTRL_DEFAULT_FPSENSOR_DETECT                                  \
	{                                                                  \
		.fpsensor_detect_get_type_return = FP_SENSOR_TYPE_UNKNOWN, \
		.get_fp_transport_type_return = FP_TRANSPORT_TYPE_UNKNOWN, \
		.get_fp_sensor_spi_select_return =                         \
			FP_SENSOR_SPI_SELECT_UNKNOWN                       \
	}

extern struct mock_ctrl_fpsensor_detect mock_ctrl_fpsensor_detect;

#ifdef __cplusplus
}
#endif

#endif /* __MOCK_FPSENSOR_DETECT_MOCK_H */
