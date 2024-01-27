/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_PPM_COMMON_H_
#define UM_PPM_PPM_COMMON_H_

#include "include/platform.h"
#include "include/ppm.h"

#include <stdbool.h>
#include <stdint.h>

// Forward declarations.
struct ucsi_pd_driver;

enum last_error_type {
	// Error came from LPM; GET_ERROR_STATUS should query the LPM for a
	// value.
	ERROR_LPM,

	// Error came from PPM; GET_ERROR_STATUS should return directly from
	// PPM.
	ERROR_PPM,
};

// Internal data for ppm common implementation.
// Exposed for test purposes.
struct ppm_common_device {
	// Parent PD driver instance. Not OWNED.
	struct ucsi_pd_driver *pd;

	// Doorbell notification callback (and context).
	ucsi_ppm_notify *opm_notify;
	void *opm_context;

	// PPM task
	struct task_handle *ppm_task_handle;
	struct platform_mutex *ppm_lock;
	struct platform_condvar *ppm_condvar;

	// PPM state
	bool cleaning_up;
	enum ppm_states ppm_state;
	struct ppm_pending_data pending;

	// Port and status information
	uint8_t num_ports;
	struct ucsiv3_get_connector_status_data *per_port_status;

	// Port number is 7 bits. 8-th bit can be sign.
	int8_t last_connector_changed;
	int8_t last_connector_alerted;

	// Data dedicated to UCSI operation.
	struct ucsi_memory_region ucsi_data;

	// Last error status info.
	enum last_error_type last_error;
	struct ucsiv3_get_error_status_data ppm_error_result;
};

/**
 * Initialize the common PPM implementation for given PD driver.
 *
 * The PD driver should own the PPM instance and is responsible for cleaning it
 * up. The PPM will retain a pointer to the pd driver in order to execute
 * commands (and any other PD driver specific actions).
 */
struct ucsi_ppm_driver *ppm_open(struct ucsi_pd_driver *pd_driver);

#endif // UM_PPM_PPM_COMMON_H_
