/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor type identifiers */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_TYPES_H
#define __CROS_EC_FPSENSOR_FPSENSOR_TYPES_H

enum fp_sensor_type {
	FP_SENSOR_TYPE_UNKNOWN = -1,
	FP_SENSOR_TYPE_FPC,
	FP_SENSOR_TYPE_ELAN,
};

enum fp_transport_type {
	FP_TRANSPORT_TYPE_UNKNOWN = -1,
	FP_TRANSPORT_TYPE_SPI,
	FP_TRANSPORT_TYPE_UART
};

enum fp_sensor_spi_select {
	FP_SENSOR_SPI_SELECT_UNKNOWN = -1,
	FP_SENSOR_SPI_SELECT_DEVELOPMENT,
	FP_SENSOR_SPI_SELECT_PRODUCTION
};

enum finger_state {
	FINGER_NONE = 0,
	FINGER_PARTIAL = 1,
	FINGER_PRESENT = 2,
};

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_TYPES_H */
