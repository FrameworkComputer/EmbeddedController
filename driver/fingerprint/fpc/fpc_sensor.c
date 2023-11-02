/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_utils.h"

#include <stddef.h>
#if defined(CONFIG_FP_SENSOR_FPC1025) || defined(CONFIG_FP_SENSOR_FPC1035)
#include "bep/fpc_private.h"
#elif defined(CONFIG_FP_SENSOR_FPC1145)
#include "libfp/fpc_private.h"
#else
#error "Sensor type not defined!"
#endif

/*
 * TODO(b/164174822): We cannot include fpc_sensor.h here, since
 * the parent fpsensor.h header conditionally excludes fpc_sensor.h
 * and replaces its content with default macros.
 * Fix this header discrepancy.
 *
 * #include "fpc_sensor.h"
 */

int fpc_fp_maintenance(uint16_t *error_state)
{
	int rv;
	fp_sensor_info_t sensor_info;
	timestamp_t start = get_time();

	if (error_state == NULL)
		return EC_ERROR_INVAL;

	rv = fp_sensor_maintenance(fp_buffer, &sensor_info);
	CPRINTS("Maintenance took %d ms", time_since32(start) / MSEC);

	if (rv != 0) {
		/*
		 * Failure can occur if any of the fingerprint detection zones
		 * are covered (i.e., finger is on sensor).
		 */
		CPRINTS("Failed to run maintenance: %d", rv);
		return EC_ERROR_HW_INTERNAL;
	}

	*error_state |= FP_ERROR_DEAD_PIXELS(sensor_info.num_defective_pixels);
	CPRINTS("num_defective_pixels: %d", sensor_info.num_defective_pixels);

	return EC_SUCCESS;
}
