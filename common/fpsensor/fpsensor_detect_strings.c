/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "fpsensor_detect.h"

const char *fp_transport_type_to_str(enum fp_transport_type type)
{
	switch (type) {
	case FP_TRANSPORT_TYPE_UNKNOWN:
	default:
		return "UNKNOWN";
	case FP_TRANSPORT_TYPE_SPI:
		return "SPI";
	case FP_TRANSPORT_TYPE_UART:
		return "UART";
	}
}

const char *fp_sensor_type_to_str(enum fp_sensor_type type)
{
	switch (type) {
	case FP_SENSOR_TYPE_UNKNOWN:
	default:
		return "UNKNOWN";
	case FP_SENSOR_TYPE_FPC:
		return "FPC";
	case FP_SENSOR_TYPE_ELAN:
		return "ELAN";
	}
}

