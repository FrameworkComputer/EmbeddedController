/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor detection (transport and sensor). */

#ifndef __CROS_EC_FPSENSOR_DETECT_H
#define __CROS_EC_FPSENSOR_DETECT_H

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

const char *fp_transport_type_to_str(enum fp_transport_type type);
const char *fp_sensor_type_to_str(enum fp_sensor_type type);
const char *fp_sensor_spi_select_to_str(enum fp_sensor_spi_select type);
enum fp_sensor_type get_fp_sensor_type(void);
enum fp_transport_type get_fp_transport_type(void);
enum fp_sensor_spi_select get_fp_sensor_spi_select(void);

#endif /* __CROS_EC_FPSENSOR_DETECT_H */
