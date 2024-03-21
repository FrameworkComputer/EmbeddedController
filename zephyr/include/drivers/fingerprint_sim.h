/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief API for fingerprint sensor simulator
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_FINGERPRINT_SIM_H_
#define ZEPHYR_INCLUDE_DRIVERS_FINGERPRINT_SIM_H_

#include <drivers/fingerprint.h>

struct fingerprint_sensor_state {
	uint16_t bad_pixels;
	bool maintenance_ran;
	bool detect_mode;
	bool low_power_mode;
	enum fingerprint_finger_state finger_state;
	int init_result;
	int deinit_result;
	int config_result;
	int get_info_result;
	int acquire_image_result;
	int last_acquire_image_mode;
};

/**
 * @brief Set fingerprint sensor state
 *
 * @param dev Pointer to the device structure of the fingerprint sensor
 *	      simulator.
 * @param state New fingerprint sensor state.
 */
__syscall void fingerprint_set_state(const struct device *dev,
				     struct fingerprint_sensor_state *state);

/**
 * @brief Get fingerprint sensor state
 *
 * @param dev Pointer to the device structure of the fingerprint sensor
 *	      simulator.
 * @param state Fingerprint sensor state.
 */
__syscall void fingerprint_get_state(const struct device *dev,
				     struct fingerprint_sensor_state *state);

/**
 * @brief Run callback passed to fingerprint sensor driver
 *
 * @param dev Pointer to the device structure of the fingerprint sensor
 *	      simulator.
 */
__syscall void fingerprint_run_callback(const struct device *dev);

/**
 * @brief Load image to the buffer
 *
 * @param dev Pointer to the device structure of the fingerprint sensor
 *	      simulator.
 * @param image Pointer to the image buffer.
 * @param image_size Size of the buffer.
 */
__syscall void fingerprint_load_image(const struct device *dev, uint8_t *image,
				      size_t image_size);
#include <syscalls/fingerprint_sim.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_FINGERPRINT_SIM_H_ */
