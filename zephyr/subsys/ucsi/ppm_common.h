/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_PPM_COMMON_H_
#define UM_PPM_PPM_COMMON_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#include <drivers/ucsi_v3.h>
#include <usbc/ppm.h>

/* Helper functions for testing. */
#ifdef CONFIG_ZTEST
/* Get the current state of the state machine. */
enum ppm_states ppm_test_get_state(const struct ucsi_ppm_device *device);

/* Checks whether an async event is pending in state machine. */
bool ppm_test_is_async_pending(struct ucsi_ppm_device *device);

/* Checks whether a command is pending in the state machine. */
bool ppm_test_is_cmd_pending(struct ucsi_ppm_device *device);

#endif

/**
 * Initialize the data for common PPM implementation using given PD driver.
 *
 * The PD driver should own the PPM instance and is responsible for cleaning it
 * up. The PPM will retain a pointer to the pd driver in order to execute
 * commands (and any other PD driver specific actions).
 */
struct ucsi_ppm_device *ppm_data_init(const struct ucsi_pd_driver *pd_driver,
				      const struct device *ppm_device,
				      union connector_status_t *data,
				      int num_ports);

#endif /* UM_PPM_PPM_COMMON_H_ */
