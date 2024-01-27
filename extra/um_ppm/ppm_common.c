/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/pd_driver.h"
#include "include/platform.h"
#include "include/ppm.h"
#include "ppm_common.h"

const char *ppm_state_strings[PPM_STATE_MAX] = {
	"PPM_STATE_NOT_READY",	    "PPM_STATE_IDLE",
	"PPM_STATE_IDLE_NOTIFY",    "PPM_STATE_PROCESSING_COMMAND",
	"PPM_STATE_WAITING_CC_ACK", "PPM_STATE_WAITING_ASYNC_EV_ACK",
};

const char *ppm_state_to_string(int state)
{
	if (state < PPM_STATE_NOT_READY || state >= PPM_STATE_MAX) {
		return "PPM_STATE_Outside_valid_range";
	}

	return ppm_state_strings[state];
}

const char *ucsi_cmd_strings[UCSI_CMD_VENDOR_CMD + 1] = {
	"UCSI_CMD_RESERVED",
	"UCSI_CMD_PPM_RESET",
	"UCSI_CMD_CANCEL",
	"UCSI_CMD_CONNECTOR_RESET",
	"UCSI_CMD_ACK_CC_CI",
	"UCSI_CMD_SET_NOTIFICATION_ENABLE",
	"UCSI_CMD_GET_CAPABILITY",
	"UCSI_CMD_GET_CONNECTOR_CAPABILITY",
	"UCSI_CMD_SET_CCOM",
	"UCSI_CMD_SET_UOR",
	"obsolete_UCSI_CMD_SET_PDM",
	"UCSI_CMD_SET_PDR",
	"UCSI_CMD_GET_ALTERNATE_MODES",
	"UCSI_CMD_GET_CAM_SUPPORTED",
	"UCSI_CMD_GET_CURRENT_CAM",
	"UCSI_CMD_SET_NEW_CAM",
	"UCSI_CMD_GET_PDOS",
	"UCSI_CMD_GET_CABLE_PROPERTY",
	"UCSI_CMD_GET_CONNECTOR_STATUS",
	"UCSI_CMD_GET_ERROR_STATUS",
	"UCSI_CMD_SET_POWER_LEVEL",
	"UCSI_CMD_GET_PD_MESSAGE",
	"UCSI_CMD_GET_ATTENTION_VDO",
	"UCSI_CMD_reserved_0x17",
	"UCSI_CMD_GET_CAM_CS",
	"UCSI_CMD_LPM_FW_UPDATE_REQUEST",
	"UCSI_CMD_SECURITY_REQUEST",
	"UCSI_CMD_SET_RETIMER_MODE",
	"UCSI_CMD_SET_SINK_PATH",
	"UCSI_CMD_SET_PDOS",
	"UCSI_CMD_READ_POWER_LEVEL",
	"UCSI_CMD_CHUNKING_SUPPORT",
	"UCSI_CMD_VENDOR_CMD",
};

const char *ucsi_command_to_string(uint8_t command)
{
	if (command > UCSI_CMD_VENDOR_CMD) {
		return "UCSI_CMD_Outside_valid_range";
	}

	return ucsi_cmd_strings[command];
}

#define DEV_CAST_FROM(v) (struct ppm_common_device *)(v)

static void clear_cci(struct ppm_common_device *dev)
{
	platform_memset(&dev->ucsi_data.cci, 0, sizeof(struct ucsi_cci));
}

static void clear_last_error(struct ppm_common_device *dev)
{
	dev->last_error = ERROR_LPM;
	platform_memset(&dev->ppm_error_result, 0,
			sizeof(struct ucsiv3_get_error_status_data));
}

inline static void set_cci_error(struct ppm_common_device *dev)
{
	clear_cci(dev);
	dev->ucsi_data.cci.error = 1;
	dev->ucsi_data.cci.cmd_complete = 1;
}

static bool is_pending_async_event(struct ppm_common_device *dev)
{
	return dev->pending.async_event;
}

static int ppm_common_opm_notify(struct ppm_common_device *dev)
{
	if (dev->opm_notify) {
		uint32_t cci;

		platform_memcpy(&cci, &dev->ucsi_data.cci, sizeof(uint32_t));
		DLOG("Notifying with CCI = 0x%08x", cci);
		dev->opm_notify(dev->opm_context);
		return 0;
	} else {
		ELOG("User error: No notifier!");
	}

	return -1;
}

static void clear_pending_command(struct ppm_common_device *dev)
{
	if (dev->pending.command) {
		DLOG("Cleared pending command[0x%x]",
		     dev->ucsi_data.control.command);
	}
	dev->pending.command = 0;
}

static void ppm_common_handle_async_event(struct ppm_common_device *dev)
{
	uint8_t port;
	struct ucsiv3_get_connector_status_data *port_status;
	bool alert_port = false;

	// Handle any smbus alert.
	if (dev->pending.async_event) {
		DLOG("PPM: Saw async event and processing.");

		// If we are in the not ready or IDLE (no notifications) state,
		// we do not bother updating OPM with status. Just clear the
		// async event and move on.
		if (dev->ppm_state == PPM_STATE_NOT_READY ||
		    dev->ppm_state == PPM_STATE_IDLE) {
			dev->pending.async_event = 0;
			return;
		}

		// Read per-port status if this is a fresh async event from an
		// LPM alert.
		if (dev->last_connector_alerted != -1) {
			DLOG("Calling GET_CONNECTOR_STATUS on port %d",
			     dev->last_connector_alerted);

			struct ucsi_control get_cs_cmd;
			platform_memset((void *)&get_cs_cmd, 0,
					sizeof(struct ucsi_control));

			get_cs_cmd.command = UCSI_CMD_GET_CONNECTOR_STATUS;
			get_cs_cmd.data_length = 0x0;
			get_cs_cmd.command_specific[0] =
				dev->last_connector_alerted;

			// Clear port status before reading.
			port = dev->last_connector_alerted - 1;
			port_status = &dev->per_port_status[port];
			platform_memset(
				port_status, 0,
				sizeof(struct ucsiv3_get_connector_status_data));

			if (dev->pd->execute_cmd(dev->pd->dev, &get_cs_cmd,
						 (uint8_t *)port_status) ==
			    -1) {
				ELOG("Failed to read port %d status. No recovery.",
				     port + 1);
			} else {
				DLOG("Port status change on %d: 0x%x", port + 1,
				     (uint16_t)port_status
					     ->connector_status_change);
			}

			// We got alerted with a change for a port we already
			// sent notifications for but which has not yet acked.
			// Resend the notification.
			if (port == dev->last_connector_changed) {
				alert_port = true;
			}

			dev->last_connector_alerted = -1;
		}

		// If we are not already acting on an existing connector change,
		// notify the OS if there are any other connector changes.
		if (dev->last_connector_changed == -1) {
			// Find the first port with any pending change.
			for (port = 0; port < dev->num_ports; ++port) {
				if (dev->per_port_status[port]
					    .connector_status_change != 0) {
					break;
				}
			}

			// Handle events in order by setting CCI and notifying
			// OPM.
			if (port < dev->num_ports) {
				alert_port = true;
			} else {
				DLOG("No more ports needing OPM alerting");
			}
		}

		// Should we alert?
		if (alert_port) {
			DLOG("Notifying async event for port %d and changing state from %d (%s)",
			     port + 1, dev->ppm_state,
			     ppm_state_to_string(dev->ppm_state));
			// Notify the OPM that we have data for it to read.
			clear_cci(dev);
			dev->last_connector_changed = port;
			dev->ucsi_data.cci.connector_changed = port + 1;
			ppm_common_opm_notify(dev);

			// Set PPM state to waiting for async event ack
			dev->ppm_state = PPM_STATE_WAITING_ASYNC_EV_ACK;
		}

		// Clear the pending bit.
		dev->pending.async_event = 0;
	}
}

static bool is_pending_command(struct ppm_common_device *dev)
{
	return dev->pending.command;
}

static bool match_pending_command(struct ppm_common_device *dev,
				  uint8_t command)
{
	return dev->pending.command &&
	       dev->ucsi_data.control.command == command;
}

static int ppm_common_execute_pending_cmd(struct ppm_common_device *dev)
{
	struct ucsi_control *control = &dev->ucsi_data.control;
	struct ucsi_cci *cci = &dev->ucsi_data.cci;
	uint8_t *message_in = (uint8_t *)&dev->ucsi_data.message_in;
	uint8_t ucsi_command = control->command;
	int ret = -1;
	bool ack_ci = false;

	if (control->command == 0 || control->command > UCSI_CMD_VENDOR_CMD) {
		ELOG("Invalid command 0x%x", control->command);

		// Set error condition to invalid command.
		clear_last_error(dev);
		dev->last_error = ERROR_PPM;
		dev->ppm_error_result.error_information.unrecognized_command =
			1;
		set_cci_error(dev);
		return -1;
	}

	switch (ucsi_command) {
	case UCSI_CMD_ACK_CC_CI:
		struct ucsiv3_ack_cc_ci_cmd *ack_cmd =
			(struct ucsiv3_ack_cc_ci_cmd *)control->command_specific;
		// The ack should already validated before we reach here.
		ack_ci = ack_cmd->connector_change_ack;
		break;

	case UCSI_CMD_GET_ERROR_STATUS:
		// If the error status came from the PPM, return the cached
		// value and skip the |execute_cmd| in the pd_driver.
		if (dev->last_error == ERROR_PPM) {
			ret = sizeof(struct ucsiv3_get_error_status_data);
			platform_memcpy(message_in, &dev->ppm_error_result,
					ret);
			goto success;
		}
		break;
	default:
		break;
	}

	// Do driver specific execute command.
	ret = dev->pd->execute_cmd(dev->pd->dev, control, message_in);

	// Clear command since we just executed it.
	platform_memset(control, 0, sizeof(struct ucsi_control));

	if (ret < 0) {
		ELOG("Error with UCSI command 0x%x. Return was %d",
		     ucsi_command, ret);
		clear_last_error(dev);
		dev->last_error = ERROR_LPM;
		set_cci_error(dev);
		return ret;
	}

success:
	DLOG("Completed UCSI command 0x%x (%s)", ucsi_command,
	     ucsi_command_to_string(ucsi_command));
	clear_cci(dev);

	// Post-success command handling
	if (ack_ci) {
		struct ucsiv3_get_connector_status_data *port_status =
			&dev->per_port_status[dev->last_connector_changed];
		// Clear port status for acked connector.
		port_status->connector_status_change = 0;
		dev->last_connector_changed = -1;
		// Flag a pending async event to process next event if it
		// exists.
		dev->pending.async_event = 1;
	}

	// If we reset, we only surface up the reset completed event after busy.
	if (ucsi_command == UCSI_CMD_PPM_RESET) {
		cci->reset_completed = 1;
	} else {
		cci->data_length = ret & 0xFF;
		cci->cmd_complete = 1;
	}
	return 0;
}

inline static bool check_ack_has_valid_bits(struct ucsiv3_ack_cc_ci_cmd *cmd)
{
	return cmd->command_complete_ack || cmd->connector_change_ack;
}

inline static bool check_ack_has_valid_ci(struct ucsiv3_ack_cc_ci_cmd *cmd,
					  struct ppm_common_device *dev)
{
	return cmd->connector_change_ack ? dev->last_connector_changed != -1 :
					   1;
}

inline static bool check_ack_has_valid_cc(struct ucsiv3_ack_cc_ci_cmd *cmd,
					  struct ppm_common_device *dev)
{
	return cmd->command_complete_ack ?
		       dev->ppm_state == PPM_STATE_WAITING_CC_ACK :
		       1;
}

inline static bool is_invalid_ack(struct ppm_common_device *dev)
{
	struct ucsiv3_ack_cc_ci_cmd *cmd =
		(struct ucsiv3_ack_cc_ci_cmd *)
			dev->ucsi_data.control.command_specific;
	return (!(check_ack_has_valid_bits(cmd) &&
		  check_ack_has_valid_ci(cmd, dev) &&
		  check_ack_has_valid_cc(cmd, dev)));
}

static void invalid_ack_notify(struct ppm_common_device *dev)
{
	struct ucsiv3_ack_cc_ci_cmd *cmd =
		(struct ucsiv3_ack_cc_ci_cmd *)
			dev->ucsi_data.control.command_specific;
	ELOG("Invalid ack usage (CI=%d CC=%d last_connector_changed=%d) in "
	     "state %d",
	     cmd->connector_change_ack, cmd->command_complete_ack,
	     dev->last_connector_changed, dev->ppm_state);

	clear_last_error(dev);
	dev->last_error = ERROR_PPM;
	dev->ppm_error_result.error_information.invalid_cmd_specific_params = 1;

	set_cci_error(dev);
	// TODO(UCSI WG): Clarify pending clear behavior in case of PPM error
	clear_pending_command(dev);
	ppm_common_opm_notify(dev);
}

// Handle pending command. When handling pending commands, it is recommended
// that dev->ppm_state changes or notifications are made only in
// this function. Error bits may be set by other functions.
static void ppm_common_handle_pending_command(struct ppm_common_device *dev)
{
	uint8_t next_command = 0;
	int ret;

	if (dev->pending.command) {
		// Check what command is currently pending.
		next_command = dev->ucsi_data.control.command;

		DLOG("PEND_CMD: Started command processing in state %d (%s), cmd 0x%x (%s)",
		     dev->ppm_state, ppm_state_to_string(dev->ppm_state),
		     next_command, ucsi_command_to_string(next_command));
		switch (dev->ppm_state) {
		case PPM_STATE_IDLE:
		case PPM_STATE_IDLE_NOTIFY:
			// We are now processing the command. Change state,
			// notify OPM and then continue.
			dev->ppm_state = PPM_STATE_PROCESSING_COMMAND;
			clear_cci(dev);
			dev->ucsi_data.cci.busy = 1;
			ppm_common_opm_notify(dev);
			// Intentional fallthrough since we are now processing.
		case PPM_STATE_PROCESSING_COMMAND:
			// TODO - Handle the case where we have a command that
			// takes multiple smbus calls to process (i.e. firmware
			// update). If we were handling something that requires
			// processing (i.e. firmware update), we would not
			// update to WAITING_CC_ACK until it was completed.
			ret = ppm_common_execute_pending_cmd(dev);
			if (ret == -1) {
				// CCI error bits are handled by
				// execute_pending_command
				dev->ppm_state = PPM_STATE_IDLE_NOTIFY;
				ppm_common_opm_notify(dev);
				break;
			}

			// If we were handling a PPM Reset, we go straight back
			// to idle and clear any error indicators.
			if (next_command == UCSI_CMD_PPM_RESET) {
				dev->ppm_state = PPM_STATE_IDLE;
				clear_last_error(dev);
			} else if (next_command == UCSI_CMD_ACK_CC_CI) {
				// We've received a standalone CI ack after
				// completing command loop(s).
				dev->ppm_state = PPM_STATE_IDLE_NOTIFY;

				clear_cci(dev);
				dev->ucsi_data.cci.ack_command = 1;
			} else {
				dev->ppm_state = PPM_STATE_WAITING_CC_ACK;
			}

			// Notify OPM to handle result and wait for ack if we're
			// not still processing.
			if (dev->ppm_state != PPM_STATE_PROCESSING_COMMAND) {
				ppm_common_opm_notify(dev);
			}
			break;
		case PPM_STATE_WAITING_CC_ACK:
		case PPM_STATE_WAITING_ASYNC_EV_ACK:
			// If we successfully ACK, update CCI and notify. On
			// error, the CCI will already be set by
			// |ppm_common_execute_pending_cmd|.
			ret = ppm_common_execute_pending_cmd(dev);
			if (ret != -1) {
				dev->ppm_state = PPM_STATE_IDLE_NOTIFY;

				clear_cci(dev);
				dev->ucsi_data.cci.ack_command = 1;
			}

			ppm_common_opm_notify(dev);
			break;
		default:
			ELOG("Unhandled ppm state (%d) when handling pending command",
			     dev->ppm_state);
			break;
		}

		DLOG("PEND_CMD: Ended command processing in state %d (%s)",
		     dev->ppm_state, ppm_state_to_string(dev->ppm_state));

		// Last thing is to clear the pending command bit before
		// executing the command.
		if (dev->ppm_state != PPM_STATE_PROCESSING_COMMAND) {
			clear_pending_command(dev);
		}
	}
}

static void ppm_common_task(void *context)
{
	struct ppm_common_device *dev = DEV_CAST_FROM(context);

	if (!dev) {
		ELOG("Cannot start PPM task without valid device pointer: %p",
		     dev);
		return;
	}

	DLOG("PPM: Starting the ppm task");

	platform_mutex_lock(dev->ppm_lock);

	// Initialize the system state.
	dev->ppm_state = PPM_STATE_NOT_READY;

	// Send PPM reset and set state to IDLE if successful.
	platform_memset(&dev->ucsi_data.control, 0,
			sizeof(struct ucsi_control));
	dev->ucsi_data.control.command = UCSI_CMD_PPM_RESET;
	if (dev->pd->execute_cmd(dev->pd->dev, &dev->ucsi_data.control,
				 dev->ucsi_data.message_in) != -1) {
		dev->ppm_state = PPM_STATE_IDLE;
		platform_memset(&dev->ucsi_data.cci, 0,
				sizeof(struct ucsi_cci));
	}

	// TODO - Note to self
	//
	// Smbus function calls are currently done with PPM lock; may need to
	// fix that.
	do {
		// Wait for a task from OPM unless we are already processing a
		// command.
		if (dev->ppm_state != PPM_STATE_PROCESSING_COMMAND) {
			DLOG("Waiting for next command at state %d (%s)...",
			     dev->ppm_state,
			     ppm_state_to_string(dev->ppm_state));
			platform_condvar_wait(dev->ppm_condvar, dev->ppm_lock);
		}

		DLOG("Handling next task at state %d (%s)", dev->ppm_state,
		     ppm_state_to_string(dev->ppm_state));

		switch (dev->ppm_state) {
		// Idle with notifications enabled.
		case PPM_STATE_IDLE:
			if (is_pending_command(dev)) {
				// Only handle SET_NOTIFICATION_ENABLE or
				// PPM_RESET. Otherwise clear the pending
				// command.
				if (match_pending_command(
					    dev,
					    UCSI_CMD_SET_NOTIFICATION_ENABLE) ||
				    match_pending_command(dev,
							  UCSI_CMD_PPM_RESET)) {
					ppm_common_handle_pending_command(dev);
				} else {
					clear_pending_command(dev);
				}
			} else if (is_pending_async_event(dev)) {
				ppm_common_handle_async_event(dev);
			}
			break;

		// Idle and waiting for a command or event.
		case PPM_STATE_IDLE_NOTIFY:
			// Check if you're acking in the right state for
			// ACK_CC_CI. Only CI acks are allowed here. i.e. we are
			// still waiting for a CI ack after a command loop was
			// completed.
			if (is_pending_command(dev) &&
			    match_pending_command(dev, UCSI_CMD_ACK_CC_CI) &&
			    is_invalid_ack(dev)) {
				invalid_ack_notify(dev);
				break;
			}

			if (is_pending_command(dev)) {
				ppm_common_handle_pending_command(dev);
			} else if (is_pending_async_event(dev)) {
				ppm_common_handle_async_event(dev);
			}
			break;

		// Processing a command. We only ever enter this state for
		// firmware update (for example if we're breaking up a chunk of
		// firmware into multiple transactions).
		case PPM_STATE_PROCESSING_COMMAND:
			ppm_common_handle_pending_command(dev);
			break;

		// Waiting for a command completion acknowledge.
		case PPM_STATE_WAITING_CC_ACK:
			if (!match_pending_command(dev, UCSI_CMD_ACK_CC_CI) ||
			    is_invalid_ack(dev)) {
				invalid_ack_notify(dev);
				break;
			}
			ppm_common_handle_pending_command(dev);
			break;

		// Waiting for async event ack.
		case PPM_STATE_WAITING_ASYNC_EV_ACK:
			if (is_pending_command(dev)) {
				bool is_ack = match_pending_command(
					dev, UCSI_CMD_ACK_CC_CI);
				if (is_ack && is_invalid_ack(dev)) {
					invalid_ack_notify(dev);
					break;
				}
				// Waiting ASYNC_EV_ACK is a weird state. It can
				// directly ACK the CI or it can go into a
				// PROCESSING_COMMAND state (in which case it
				// should be treated as a IDLE_NOTIFY).
				//
				// Thus, if we don't get UCSI_CMD_ACK_CC_CI
				// here, we just treat this as IDLE_NOTIFY
				// state.
				if (!is_ack) {
					DLOG("ASYNC EV ACK state turned into IDLE_NOTIFY state");
					dev->ppm_state = PPM_STATE_IDLE_NOTIFY;
				}
				ppm_common_handle_pending_command(dev);
			}
			break;

		default:
			break;
		}
	} while (!dev->cleaning_up);

	platform_mutex_unlock(dev->ppm_lock);

	platform_task_exit();
}

static int ppm_common_init_and_wait(struct ucsi_ppm_device *device,
				    uint8_t num_ports)
{
#define MAX_TIMEOUT_MS 1000
#define POLL_EVERY_MS 10
	struct ppm_common_device *dev = DEV_CAST_FROM(device);
	struct ucsi_memory_region *ucsi_data = &dev->ucsi_data;
	bool ready_to_exit = false;

	// First clear the PPM shared memory region.
	platform_memset(ucsi_data, 0, sizeof(*ucsi_data));

	// Initialize to UCSI version 3.0
	ucsi_data->version.version = 0x0300;
	// TODO - Set real lpm address based on smbus driver.
	ucsi_data->version.lpm_address = 0x0;

	// Init lock to sync PPM task and main task context.
	dev->ppm_lock = platform_mutex_init();
	if (!dev->ppm_lock) {
		return -1;
	}

	// Init condvar to notify PPM task.
	dev->ppm_condvar = platform_condvar_init();
	if (!dev->ppm_condvar) {
		return -1;
	}

	// Allocate per port status (used for PPM async event notifications).
	dev->num_ports = num_ports;
	dev->per_port_status = platform_calloc(
		dev->num_ports,
		sizeof(struct ucsiv3_get_connector_status_data));
	dev->last_connector_changed = -1;

	DLOG("Ready to initialize PPM task!");

	// Initialize the PPM task.
	dev->ppm_task_handle =
		platform_task_init((void *)ppm_common_task, (void *)dev);
	if (!dev->ppm_task_handle) {
		ELOG("No ppm task created.");
		return -1;
	}

	DLOG("PPM is waiting for task to run.");

	for (int count = 0; count * POLL_EVERY_MS < MAX_TIMEOUT_MS; count++) {
		platform_mutex_lock(dev->ppm_lock);
		ready_to_exit = dev->ppm_state != PPM_STATE_NOT_READY;
		platform_mutex_unlock(dev->ppm_lock);

		if (ready_to_exit) {
			break;
		}

		platform_usleep(POLL_EVERY_MS * 1000);
	}

	DLOG("PPM initialized result: Success=%b", ready_to_exit);

	return (ready_to_exit ? 0 : -1);
}

struct ucsi_memory_region *
ppm_common_get_data_region(struct ucsi_ppm_device *device)
{
	struct ppm_common_device *dev = DEV_CAST_FROM(device);
	return &dev->ucsi_data;
}

bool ppm_common_get_next_connector_status(
	struct ucsi_ppm_device *device, uint8_t *out_port_num,
	struct ucsiv3_get_connector_status_data **out_connector_status)
{
	struct ppm_common_device *dev = DEV_CAST_FROM(device);

	if (dev->last_connector_changed != -1) {
		*out_port_num = (uint8_t)dev->last_connector_changed + 1;
		*out_connector_status =
			&dev->per_port_status[dev->last_connector_changed];
		return true;
	}

	return false;
}

static int ppm_common_read(struct ucsi_ppm_device *device, unsigned int offset,
			   void *buf, size_t length)
{
	struct ppm_common_device *dev = DEV_CAST_FROM(device);

	if (!dev) {
		return -1;
	}

	// Validate memory to read and allow any offset for reading.
	if (offset + length >= sizeof(struct ucsi_memory_region)) {
		ELOG("UCSI read exceeds bounds of memory: offset(0x%x), length(0x%x)",
		     offset, length);
		return -1;
	}

	platform_memcpy(buf,
			(const void *)((uint8_t *)(&dev->ucsi_data) + offset),
			length);
	return length;
}

static int ppm_common_handle_control_message(struct ppm_common_device *dev,
					     const void *buf, size_t length)
{
	const uint8_t *cmd = (const uint8_t *)buf;
	uint8_t prev_cmd;
	uint8_t busy = 0;

	if (length > sizeof(struct ucsi_control)) {
		ELOG("Tried to send control message that is an invalid size (%d)",
		     (int)length);
		return -1;
	}

	// If we're currently sending a command, we should immediately discard
	// this call.
	{
		platform_mutex_lock(dev->ppm_lock);
		busy = dev->pending.command || dev->ucsi_data.cci.busy;
		prev_cmd = dev->ucsi_data.control.command;
		platform_mutex_unlock(dev->ppm_lock);
	}
	if (busy) {
		ELOG("Tried to send control message (cmd=0x%x) when one is already pending "
		     "(cmd=0x%x).",
		     cmd[0], prev_cmd);
		return -1;
	}

	// If we didn't get a full CONTROL message, zero the region before
	// copying.
	if (length != sizeof(struct ucsi_control)) {
		platform_memset(&dev->ucsi_data.control, 0,
				sizeof(struct ucsi_control));
	}
	platform_memcpy(&dev->ucsi_data.control, cmd, length);

	DLOG("Got valid control message: 0x%x (%s)", cmd[0],
	     ucsi_command_to_string(cmd[0]));

	// Schedule command send.
	{
		platform_mutex_lock(dev->ppm_lock);

		// Mark command pending.
		dev->pending.command = 1;
		platform_condvar_signal(dev->ppm_condvar);

		DLOG("Signaled pending command");

		platform_mutex_unlock(dev->ppm_lock);
	}

	return 0;
}

/*
 Only allow writes into two regions:
 - Control (to send commands)
 - Message Out (to prepare data to send commands)

 A control message will result in an actual UCSI command being called if the
 data is valid.

 A write into message in doesn't modify the PPM state but is often
 a precursor to actually sending a control message. This will be used for fw
 updates.

 Any writes into non-aligned offsets (except Message IN) will be discarded.
*/
static int ppm_common_write(struct ucsi_ppm_device *device, unsigned int offset,
			    const void *buf, size_t length)
{
	struct ppm_common_device *dev = DEV_CAST_FROM(device);
	bool valid_fixed_offset;

	if (!buf || length == 0) {
		ELOG("Invalid buffer (%p) or length (%x)", buf, length);
		return -1;
	}

	valid_fixed_offset = (offset == UCSI_VERSION_OFFSET) ||
			     (offset == UCSI_CCI_OFFSET) ||
			     (offset == UCSI_CONTROL_OFFSET);

	if (!valid_fixed_offset &&
	    !(offset >= UCSI_MESSAGE_OUT_OFFSET &&
	      offset < UCSI_MESSAGE_OUT_OFFSET + MESSAGE_OUT_SIZE)) {
		ELOG("UCSI can't write to invalid offset: 0x%x", offset);
		return -1;
	}

	// Handle control messages
	if (offset == UCSI_CONTROL_OFFSET) {
		return ppm_common_handle_control_message(dev, buf, length);
	}

	if (offset >= UCSI_MESSAGE_OUT_OFFSET &&
	    offset + length >= UCSI_MESSAGE_OUT_OFFSET + MESSAGE_OUT_SIZE) {
		ELOG("UCSI write to MESSAGE_OUT exceeds bounds: offset(0x%x) + size(0x%x) "
		     ">= "
		     "end(0x%x)",
		     offset, length,
		     UCSI_MESSAGE_OUT_OFFSET + MESSAGE_OUT_SIZE);
		return -1;
	}

	// Copy from input buffer to offset within MESSAGE_OUT.
	platform_memcpy(dev->ucsi_data.message_out +
				(offset - UCSI_MESSAGE_OUT_OFFSET),
			buf, length);
	return 0;
}

static int ppm_common_register_notify(struct ucsi_ppm_device *device,
				      ucsi_ppm_notify *callback, void *context)
{
	struct ppm_common_device *dev = DEV_CAST_FROM(device);
	if (!dev) {
		return -1;
	}

	dev->opm_notify = callback;
	dev->opm_context = context;
	return 0;
}

static void ppm_common_lpm_alert(struct ucsi_ppm_device *device, uint8_t lpm_id)
{
	struct ppm_common_device *dev = DEV_CAST_FROM(device);

	DLOG("LPM alert seen on port %d!", lpm_id);

	platform_mutex_lock(dev->ppm_lock);

	if (lpm_id <= dev->num_ports) {
		// Set async event and mark port status as not read.
		dev->pending.async_event = 1;
		dev->last_connector_alerted = lpm_id;

		platform_condvar_signal(dev->ppm_condvar);
	} else {
		ELOG("Alert id out of range: %d (num_ports = %d)", lpm_id,
		     dev->num_ports);
	}

	platform_mutex_unlock(dev->ppm_lock);
}

static void ppm_common_cleanup(struct ucsi_ppm_driver *driver)
{
	if (driver->dev) {
		struct ppm_common_device *dev = DEV_CAST_FROM(driver->dev);

		// Signal clean up to waiting thread.
		platform_mutex_lock(dev->ppm_lock);
		dev->cleaning_up = true;
		platform_condvar_signal(dev->ppm_condvar);
		platform_mutex_unlock(dev->ppm_lock);

		// Wait for task to complete.
		platform_task_complete(dev->ppm_task_handle);

		platform_free(dev->ppm_condvar);
		platform_free(dev->ppm_lock);

		platform_free(driver->dev);
		driver->dev = NULL;
	}
}

struct ucsi_ppm_driver *ppm_open(struct ucsi_pd_driver *pd_driver)
{
	struct ppm_common_device *dev = NULL;
	struct ucsi_ppm_driver *drv = NULL;

	dev = platform_calloc(1, sizeof(struct ppm_common_device));
	if (!dev) {
		goto handle_error;
	}

	dev->pd = pd_driver;

	drv = platform_calloc(1, sizeof(struct ucsi_ppm_driver));
	if (!drv) {
		goto handle_error;
	}

	drv->dev = (struct ucsi_ppm_device *)dev;
	drv->init_and_wait = ppm_common_init_and_wait;
	drv->get_data_region = ppm_common_get_data_region;
	drv->get_next_connector_status = ppm_common_get_next_connector_status;
	drv->read = ppm_common_read;
	drv->write = ppm_common_write;
	drv->register_notify = ppm_common_register_notify;
	drv->lpm_alert = ppm_common_lpm_alert;
	drv->cleanup = ppm_common_cleanup;

	return drv;

handle_error:
	platform_free(dev);
	platform_free(drv);

	return NULL;
}
