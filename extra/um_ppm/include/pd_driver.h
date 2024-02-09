/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_INCLUDE_PD_DRIVER_H_
#define UM_PPM_INCLUDE_PD_DRIVER_H_

#include "include/ppm.h"

#include <stddef.h>
#include <stdint.h>

/* Internal data structure for pd_driver implementations. */
struct ucsi_pd_device;

/* Forward declaration only. */
struct ucsi_pd_driver;
struct ucsi_ppm_driver;

/**
 * Configure LPM IRQ for this PD driver.
 *
 * Initialize and configure LPM IRQ handling for this PD driver. Interrupts that
 * occur before the PPM is initialized will be dropped (as there is nothing to
 * do with them).
 *
 * @return 0 if IRQ is configured (or already configured). -1 on error.
 */
typedef int(ucsi_pd_configure_lpm_irq)(struct ucsi_pd_device *dev);

/**
 * Initialize the PPM associated with this PD driver.
 *
 * This will block until the PPM is ready to be used. Call this after
 * registering OPM and LPM mechanisms.
 *
 * @param dev: Device object for this PD controller.
 *
 * @return 0 on success, -1 on error.
 */
typedef int(ucsi_pd_init_ppm)(struct ucsi_pd_device *dev);

/**
 * Grab a pointer to the PPM.
 *
 * @param dev: Device object for this PD controller.
 */
typedef struct ucsi_ppm_driver *(ucsi_pd_get_ppm)(struct ucsi_pd_device *dev);

/**
 * Execute a command in the PPM.
 *
 * While the PPM handles the overall OPM<->PPM interaction, this method is
 * called by the PPM in order to actually send the command to the LPM and handle
 * the response. This method should not modify the CCI and let the PPM
 * implementation do so instead.
 *
 * @param dev: Device object for this PD controller.
 * @param control: Command to execute.
 * @param lpm_data_out: Data returned from LPM.
 *
 * @returns -1 on error or the number of bytes read on success.
 */
typedef int(ucsi_pd_execute_command)(struct ucsi_pd_device *dev,
				     struct ucsi_control *control,
				     uint8_t *lpm_data_out);

/**
 * Get the number of ports supported by this PD device.
 *
 * @param dev: Device object for this PD controller.
 *
 * @returns -1 on error or the number of active ports.
 */
typedef int(ucsi_pd_get_active_port_count)(struct ucsi_pd_device *dev);

/**
 * Clean up the given PD driver. Call before freeing.
 *
 * @param driver: Driver object to clean up.
 */
typedef void(ucsi_pd_cleanup)(struct ucsi_pd_driver *driver);

/**
 * General driver for PD controllers.
 *
 * When constructing, must be provided a PPM implementation.
 */
struct ucsi_pd_driver {
	struct ucsi_pd_device *dev;

	ucsi_pd_configure_lpm_irq *configure_lpm_irq;
	ucsi_pd_init_ppm *init_ppm;
	ucsi_pd_get_ppm *get_ppm;
	ucsi_pd_execute_command *execute_cmd;
	ucsi_pd_get_active_port_count *get_active_port_count;

	ucsi_pd_cleanup *cleanup;
};

enum lpm_transport {
	SMBUS,
};

/* Maximum number of addressable ports via PPM. The actual maximum depends on
 * the PD topology and controllers used. (i.e. Two 2-port controllers would
 * support 4 addressable ports)
 */
#define MAX_PORTS_SUPPORTED 8

/**
 * Configuration data for a PD controller.
 */
struct pd_driver_config {
	/* Maximum number of addresses supported by this pd driver.*/
	uint8_t max_num_ports;

	/* Map of port number to port id. Will be used for distinguishing ports
	 * at the LPM.
	 */
	uint8_t port_address_map[MAX_PORTS_SUPPORTED];

	/* What transport is used for the LPM. */
	enum lpm_transport transport;
};

#endif /* UM_PPM_INCLUDE_PD_DRIVER_H_ */
