/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SUBSYS_UCSI_INCLUDE_PPM_H_
#define ZEPHYR_SUBSYS_UCSI_INCLUDE_PPM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#include <drivers/ucsi_v3.h>

/* Steady-state PPM states.
 *
 * Use to keep track of states that won't immediately be updated synchronously
 * but may persist waiting for some communication with either the OPM or LPM.
 */
enum ppm_states {
	/* Only handle PPM_RESET or async event for PPM reset.
	 * This is the default state before we are ready to handle any OPM
	 * commands.
	 */
	PPM_STATE_NOT_READY,

	/* Only accept Set Notification Enable. Everything else no-ops. */
	PPM_STATE_IDLE,

	/* Handle most commands. */
	PPM_STATE_IDLE_NOTIFY,

	/* Unused state. */
	/* PPM_STATE_BUSY, */

	/* PPM_STATE_PROCESS_COMMAND is a hidden state that happens
	 * synchronously.
	 */

	/* Processing current command. */
	PPM_STATE_PROCESSING_COMMAND,

	/* Waiting for command complete acknowledgment from OPM. */
	PPM_STATE_WAITING_CC_ACK,
	/* PPM_STATE_PROCESS_CC_ACK, */

	/* Waiting for async event acknowledgment from OPM. */
	PPM_STATE_WAITING_ASYNC_EV_ACK,
	/* PPM_STATE_PROCESS_ASYNC_EV_ACK, */

	/* PPM_STATE_CANCELLING_COMMAND, */

	/* Just for bounds checking. */
	PPM_STATE_MAX,
};

/* Forward declarations. */
struct ucsi_ppm_device;

/**
 * Wait for the PPM to be initialized and ready for use.
 *
 * @param device: Data for PPM implementation.
 *
 * @return 0 on success and -1 on error.
 */
int ucsi_ppm_init_and_wait(struct ucsi_ppm_device *device);

/**
 * Get the next connector status if a connector change indication is
 * currently active.
 *
 * @param device: Data for PPM implementation.
 * @param out_port_num: Port number for active connector change indication.
 * @param out_connector_status: Next active connector status.
 *
 * @return True if we have pending connector change indications.
 */
bool ucsi_ppm_get_next_connector_status(
	struct ucsi_ppm_device *device, uint8_t *out_port_num,
	union connector_status_t **out_connector_status);

/**
 * Read data from UCSI at a specific data offset.
 *
 * @param device: Data for PPM implementation.
 * @param offset: Memory offset in OPM/PPM data structures.
 * @param buf: Buffer to read into.
 * @param length: Length of data to read.
 *
 * @return Bytes read or -1 for errors.
 */
int ucsi_ppm_read(struct ucsi_ppm_device *device, unsigned int offset,
		  void *buf, size_t length);

/**
 * Write data for UCSI to a specific data offset.
 *
 * @param device: Data for PPM implementation.
 * @param offset: Memory offset in OPM/PPM data structures.
 * @param buf: Buffer to write from.
 * @param length: Length of data to write.
 *
 * @return Bytes written or -1 for errors.
 */
int ucsi_ppm_write(struct ucsi_ppm_device *device, unsigned int offset,
		   const void *buf, size_t length);

/**
 * Function to send OPM a notification (doorbell).
 *
 * @param context: Context data for the OPM notifier.
 */
typedef void(ucsi_ppm_notify_cb)(void *context);

/**
 * Register a notification callback with the driver. If there is already an
 * existing callback, this will replace it.
 *
 * @param device: Data for PPM implementation.
 * @param callback: Function to call to notify OPM.
 * @param context: Context data to pass back to callback.
 *
 * @return 0 if new callback set or 1 if callback replaced.
 */
int ucsi_ppm_register_notify(struct ucsi_ppm_device *device,
			     ucsi_ppm_notify_cb *callback, void *context);

/**
 * Alert the PPM that an LPM has sent a notification.
 *
 * @param device: Data for PPM implementation.
 * @param port_id: Port on which the change was made.
 */
void ucsi_ppm_lpm_alert(struct ucsi_ppm_device *device, uint8_t port_id);

/**
 * Configure LPM IRQ for this PD driver.
 *
 * Initialize and configure LPM IRQ handling for this PD driver. Interrupts that
 * occur before the PPM is initialized will be dropped (as there is nothing to
 * do with them).
 *
 * @return 0 if IRQ is configured (or already configured). -1 on error.
 */
typedef int(ucsi_pd_configure_lpm_irq)(const struct device *dev);

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
typedef int(ucsi_pd_init_ppm)(const struct device *dev);

/**
 * Get pointer to PPM data associated with this PD driver.
 *
 * @param dev: Device object for this PD controller.
 *
 * @return PPM device pointer on success.
 */
typedef struct ucsi_ppm_device *(ucsi_pd_get_ppm_dev)(const struct device *dev);

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
typedef int(ucsi_pd_execute_command)(const struct device *dev,
				     struct ucsi_control_t *control,
				     uint8_t *lpm_data_out);

/**
 * Get the number of ports supported by this PD device.
 *
 * @param dev: Device object for this PD controller.
 *
 * @returns -1 on error or the number of active ports.
 */
typedef int(ucsi_pd_get_active_port_count)(const struct device *dev);

/**
 * Clean up the given PD driver. Call before freeing.
 *
 * @param driver: Driver object to clean up.
 */
typedef void(ucsi_pd_cleanup)(const struct device *dev);

/**
 * General driver for PD controllers.
 *
 * When constructing, must be provided a PPM implementation.
 */
struct ucsi_pd_driver {
	ucsi_pd_configure_lpm_irq *configure_lpm_irq;
	ucsi_pd_init_ppm *init_ppm;
	ucsi_pd_get_ppm_dev *get_ppm_dev;
	ucsi_pd_execute_command *execute_cmd;
	ucsi_pd_get_active_port_count *get_active_port_count;

	ucsi_pd_cleanup *cleanup;
};
#endif /* ZEPHYR_SUBSYS_UCSI_INCLUDE_PPM_H_ */
