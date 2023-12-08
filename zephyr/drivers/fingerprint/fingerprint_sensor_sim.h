/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>

struct fp_simulator_cfg {
	struct fingerprint_info info;
	uint8_t *image_buffer;
};

struct fp_simulator_data {
	fingerprint_callback_t callback;
	struct fingerprint_sensor_state state;
	uint16_t errors;
};

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_ */
