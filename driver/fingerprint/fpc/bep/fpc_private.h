/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Private sensor interface */

#ifndef __CROS_EC_FPC_PRIVATE_H
#define __CROS_EC_FPC_PRIVATE_H

#include <stdint.h>

typedef struct {
	uint32_t num_defective_pixels;
} fp_sensor_info_t;

/**
 * fp_sensor_maintenance runs a test for defective pixels and should
 * be triggered periodically by the client. Internally, a defective
 * pixel list is maintained and the algorithm will compensate for
 * any defect pixels when matching towards a template.
 *
 * The defective pixel update will abort and return an error if any of
 * the finger detect zones are covered. A client can call
 * fp_sensor_finger_status to determine the current status.
 *
 * @param[in]  image_data      pointer to FP_SENSOR_IMAGE_SIZE bytes of memory
 * @param[out] fp_sensor_info  Structure containing output data.
 *
 * @return
 * - 0 on success
 * - negative value on error
 */
int fp_sensor_maintenance(uint8_t *image_data,
			  fp_sensor_info_t *fp_sensor_info);

#endif  /* __CROS_EC_FPC_PRIVATE_H */
