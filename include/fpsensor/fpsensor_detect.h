/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor detection (transport and sensor). */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_DETECT_H
#define __CROS_EC_FPSENSOR_FPSENSOR_DETECT_H

#include "fpsensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *fp_transport_type_to_str(enum fp_transport_type type);
const char *fp_sensor_type_to_str(enum fp_sensor_type type);
const char *fp_sensor_spi_select_to_str(enum fp_sensor_spi_select type);
enum fp_sensor_type fpsensor_detect_get_type(void);
enum fp_transport_type get_fp_transport_type(void);
enum fp_sensor_spi_select fpsensor_detect_get_spi_select(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_DETECT_H */
