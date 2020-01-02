/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_FPSENSOR_DETECT_MOCK_H
#define __MOCK_FPSENSOR_DETECT_MOCK_H

#include "fpsensor_detect.h"

struct mock_ctrl_fpsensor_detect {
	enum fp_sensor_type get_fp_sensor_type_return;
	enum fp_transport_type get_fp_transport_type_return;
};

#define MOCK_CTRL_DEFAULT_FPSENSOR_DETECT {				\
	.get_fp_sensor_type_return = FP_SENSOR_TYPE_UNKNOWN,		\
	.get_fp_transport_type_return = FP_TRANSPORT_TYPE_UNKNOWN,	\
}

extern struct mock_ctrl_fpsensor_detect mock_ctrl_fpsensor_detect;

#endif /* __MOCK_FPSENSOR_DETECT_MOCK_H */
