/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/pd_driver.h"
#include "include/platform.h"
#include "include/ppm.h"
#include "ppm_common.h"
#include "rts5453.h"

#include <stdbool.h>

#define SMBUS_MAX_BLOCK_SIZE 32

#define MAX_NUM_ALT_MODES 8
#define CONSTAT_SUPPORTED_CAM_CHANGE (1 << 8)
struct virtual_mode_info {
	/* Which connector is this information for? */
	uint8_t connector;

	/* Number of valid alt modes for this connector. */
	uint8_t num_alt_modes;

	/* SVIDs for alt modes. */
	uint16_t alt_mode_svids[MAX_NUM_ALT_MODES];

	/* Current alternate mode. Use 0xFF for none. */
	uint8_t current_alt_mode;

	/* What's the target alt mode. 0xFF means no automatic entry. */
	uint8_t desired_alt_mode;

	/* Cached set_usb4 state. USB3 state is ignored for virtualization.
	 * This will be used to modify the return value for
	 * GET_CONNECTOR_CAPABILITY and GET_CONNECTOR_STATUS.
	 */
	bool allow_usb4;

	/* Mask to OR over connector status. */
	uint16_t constat_mask;
};

struct driver_action_t {
	struct platform_mutex *lock;
	struct platform_condvar *condvar;

	enum action_t {
		ACTION_NONE = 0,
		ACTION_LPM_ALERT,
		ACTION_CLEANUP_EXIT,
	} action;

	union {
		struct lpm_alert_t {
			uint8_t connector;
		} alert_action;

		uint8_t no_op;
	};
};

struct rts5453_device {
	/* LPM smbus driver. */
	struct smbus_driver *smbus;

	/* PPM driver (common implementation). */
	struct ucsi_ppm_driver *ppm;

	/* Re-usable command buffer for active command. */
	uint8_t cmd_buffer[SMBUS_MAX_BLOCK_SIZE];

	/* Configuration for this driver. */
	struct pd_driver_config *driver_config;

	/* Number of active ports from |GET_CAPABILITIES|. */
	uint8_t active_port_count;

	/* IRQ task for LPM interrupts. */
	struct task_handle *lpm_interrupt_task;

	/* Task for handling out-of-band actions. */
	struct task_handle *action_task;

	/* Next action to take in the action task. */
	struct driver_action_t action_data;

	/* Per port virtual alt-mode information. Used for emulating AP driven
	 * alt-mode.
	 */
	struct virtual_mode_info *per_port_mode_info;
};

#define CAST_FROM(v) (struct rts5453_device *)(v)

enum rts5453_smbus_commands {
	SC_VENDOR_CMD,
	SC_GET_IC_STATUS,
	SC_GET_VDO,

	SC_WRITE_FLASH_0K_64K,
	SC_WRITE_FLASH_64K_128K,
	SC_WRITE_FLASH_128K_192K,
	SC_WRITE_FLASH_192K_256K,

	SC_READ_FLASH_0K_64K,
	SC_READ_FLASH_64K_128K,
	SC_READ_FLASH_128K_192K,
	SC_READ_FLASH_192K_256K,

	SC_ERASE_FLASH,
	SC_GET_SPI_PROTECT,
	SC_SET_SPI_PROTECT,
	SC_ISP_VALIDATION,
	SC_RESET_TO_FLASH,

	/* Various ucsi commands */
	SC_UCSI_COMMANDS,
	SC_SET_NOTIFICATION_ENABLE,
	SC_ACK_CC_CI,

	SC_CMD_MAX,
};

struct rts5453_command_entry {
	int command;
	uint8_t command_value;

	/* Either 0 (for no read), -1 (variable read) or 1-32 for fixed size
	 * reads.
	 */
	size_t return_length;
};

#define CMD_ENTRY(cmd, cmd_val, ret_length)                    \
	{                                                      \
		.command = SC_##cmd, .command_value = cmd_val, \
		.return_length = ret_length                    \
	}

struct rts5453_command_entry commands[] = {
	CMD_ENTRY(VENDOR_CMD, 0x1, 0),
	CMD_ENTRY(GET_IC_STATUS, 0x3A, 32),
	CMD_ENTRY(GET_VDO, 0x08, -1),

	CMD_ENTRY(WRITE_FLASH_0K_64K, 0x04, 0),
	CMD_ENTRY(WRITE_FLASH_64K_128K, 0x06, 0),
	CMD_ENTRY(WRITE_FLASH_128K_192K, 0x13, 0),
	CMD_ENTRY(WRITE_FLASH_192K_256K, 0x14, 0),

	CMD_ENTRY(READ_FLASH_0K_64K, 0x24, -1),
	CMD_ENTRY(READ_FLASH_64K_128K, 0x26, -1),
	CMD_ENTRY(READ_FLASH_128K_192K, 0x33, -1),
	CMD_ENTRY(READ_FLASH_192K_256K, 0x34, -1),

	CMD_ENTRY(ERASE_FLASH, 0x03, -1),
	CMD_ENTRY(GET_SPI_PROTECT, 0x36, -1),
	CMD_ENTRY(SET_SPI_PROTECT, 0x07, 0),
	CMD_ENTRY(ISP_VALIDATION, 0x16, 0),
	CMD_ENTRY(RESET_TO_FLASH, 0x05, 0),

	CMD_ENTRY(UCSI_COMMANDS, 0x0E, -1),
	CMD_ENTRY(SET_NOTIFICATION_ENABLE, 0x08, 0),
	CMD_ENTRY(ACK_CC_CI, 0x0A, 0),
};

struct rts5453_ucsi_commands {
	uint8_t command;
	uint8_t command_copy_length;
};

#define UCSI_CMD_ENTRY(cmd, length)                            \
	{                                                      \
		.command = cmd, .command_copy_length = length, \
	}

struct rts5453_ucsi_commands ucsi_commands[UCSI_CMD_VENDOR_CMD + 1] = {
	UCSI_CMD_ENTRY(UCSI_CMD_RESERVED, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_PPM_RESET, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_CANCEL, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_CONNECTOR_RESET, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_ACK_CC_CI, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_NOTIFICATION_ENABLE, 3),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CAPABILITY, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CONNECTOR_CAPABILITY, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_CCOM, 2),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_UOR, 2),
	UCSI_CMD_ENTRY(obsolete_UCSI_CMD_SET_PDM, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_PDR, 2),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_ALTERNATE_MODES, 4),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CAM_SUPPORTED, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CURRENT_CAM, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_NEW_CAM, 6),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_PDOS, 3),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CABLE_PROPERTY, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CONNECTOR_STATUS, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_ERROR_STATUS, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_POWER_LEVEL, 6),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_PD_MESSAGE, 4),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_ATTENTION_VDO, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_reserved_0x17, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CAM_CS, 2),
	UCSI_CMD_ENTRY(UCSI_CMD_LPM_FW_UPDATE_REQUEST, 4),
	UCSI_CMD_ENTRY(UCSI_CMD_SECURITY_REQUEST, 5),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_RETIMER_MODE, 5),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_SINK_PATH, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_PDOS, 3),
	UCSI_CMD_ENTRY(UCSI_CMD_READ_POWER_LEVEL, 3),
	UCSI_CMD_ENTRY(UCSI_CMD_CHUNKING_SUPPORT, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_VENDOR_CMD, 6),
};

#define PING_DELAY_US 10000
#define RETRY_COUNT 200

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define RTS5453_BANK0_START 0x0
#define RTS5453_BANK0_END 0x20000
#define RTS5453_BANK1_START 0x20000
#define RTS5453_BANK1_END 0x40000

#define UCSI_7BIT_PORTMASK(p) ((p) & 0x7F)

#define RTS_PING_STATUS_MASK(s) ((s) & 0x3)
#define RTS_PING_BUSY 0
#define RTS_PING_COMPLETE 1
#define RTS_PING_DEFERRED 2
#define RTS_PING_ERROR 3
#define RTS_PING_DATA_LEN(s) ((s) >> 2)

/* Convert a given port to a chip address.
 *
 * @param dev: Internal data.
 * @param port: 1-indexed port number. 0 will give default port.
 *
 * @return Chip address for smbus.
 */
static uint8_t port_to_chip_address(struct rts5453_device *dev, uint8_t port)
{
	if (port > dev->active_port_count) {
		ELOG("Attempted to access invalid port %d. Active ports= %d",
		     port, dev->active_port_count);
		return 0;
	}

	if (port > 0) {
		port = port - 1;
	}

	return dev->driver_config->port_address_map[port];
}

static int rts5453_ping_status(struct rts5453_device *dev, uint8_t port)
{
	int retry_count;

	for (retry_count = 0; retry_count < RETRY_COUNT; ++retry_count) {
		int byte = dev->smbus->read_byte(
			dev->smbus->dev, port_to_chip_address(dev, port));

		/* Ping status failed */
		if (byte == -1) {
			ELOG("Ping status got read_error");
			return -1;
		}

		/* Busy or deferred so wait 10ms. */
		if (byte == RTS_PING_BUSY || byte == RTS_PING_DEFERRED) {
			platform_usleep(PING_DELAY_US);
			continue;
		}

		/* Valid ping status */
		DLOG("Ping status: 0x%02x", (byte & 0xff));
		return (byte & 0xFF);
	}

	DLOG("Timed out on ping status");
	return -1;
}

static int rts5453_smbus_command(struct rts5453_device *dev, uint8_t port,
				 int cmd, uint8_t *cmd_data, size_t length,
				 uint8_t *out, size_t out_length)
{
	uint8_t cmd_val;
	int read_size;
	uint8_t chip_address = port_to_chip_address(dev, port);

	if (!dev || !dev->smbus || chip_address == 0) {
		ELOG("Dev (%p), smbus(%p) or port (%u) is invalid", dev,
		     dev ? dev->smbus : NULL, port);
		return -1;
	}

	if (cmd < 0 || cmd >= SC_CMD_MAX) {
		ELOG("Invalid command sent: %d", cmd);
		return -1;
	}

	cmd_val = commands[cmd].command_value;
	read_size = commands[cmd].return_length;

	if (cmd == SC_UCSI_COMMANDS) {
		DLOG("Sending smbus command 0x%x ucsi command 0x%x", cmd_val,
		     cmd_data[0]);
	} else {
		DLOG("Sending smbus command 0x%x", cmd_val);
	}

	/* Write failed. No point in waiting on ping_status */
	if (dev->smbus->write_block(dev->smbus->dev, chip_address, cmd_val,
				    cmd_data, length) == -1) {
		ELOG("Write block for command failed");
		return -1;
	}

	/* Error out if ping status is invalid. */
	int ping_status = rts5453_ping_status(dev, port);
	if (ping_status == -1 ||
	    RTS_PING_STATUS_MASK(ping_status) == RTS_PING_ERROR) {
		ELOG("Ping status failed with %d", ping_status);
		return -1;
	}

	if (read_size != 0) {
		if (!out) {
			ELOG("No output buffer to send data");
			return -1;
		}

		read_size = read_size != -1 ? read_size :
					      RTS_PING_DATA_LEN(ping_status);

		if (read_size == 0) {
			DLOG("Nothing to read.");
			return 0;
		}

		if (read_size > out_length) {
			ELOG("Truncated read bytes for command [0x%x]. Wanted %d but input buffer "
			     "only had %d",
			     cmd_val, read_size, out_length);
			read_size = out_length;
		}

		int bytes_read = dev->smbus->read_block(
			dev->smbus->dev, chip_address, 0x80, out, out_length);
		DLOG("Read_block at 0x80 read %d bytes", bytes_read);
		return bytes_read;
	}

	DLOG("Skipped read and returning");
	return 0;
}

/* Virtualized port-partner alt-modes.
 *
 * Realtek FW versions < 0.9.x do not correct handle AP driven alt-mode. Add a
 * virtualized alt-mode handler that resets per port on connection but can be
 * used to develop the kernel driver.
 *
 * Note:
 *   Port configuration of SET_NEW_CAM(0xff, _) will be honored and alt-mode
 *   will default to 0xff (which is no-alt-mode).
 *
 *   List of valid alt-modes will be collated from any GET_ALTERNATE_MODES
 *   results from SOP and SOP'.
 *
 *   If SET_NEW_CAM(0xff, _) is not set, we will simply report entering the
 *   highest alternate mode available on connection. The ordering will be:
 *     * Thunderbolt (0x8087)
 *     * Displayport (0xff01)
 *
 *  Finally, we also virtualize the SET_USB state to enable/disable USB4. This
 *  means we will intercept the results to GET_CONNECTOR_STATUS and inject bits
 *  based on current state.
 */

static void rts5453_reset_desired_altmodes(struct rts5453_device *dev)
{
	for (uint8_t i = 0; i < dev->active_port_count; ++i) {
		uint8_t tbt_mode = 0;
		uint8_t dp_mode = 0;

		struct virtual_mode_info *vmode = &dev->per_port_mode_info[i];

		for (int j = 0; j < vmode->num_alt_modes; ++j) {
			if (vmode->alt_mode_svids[j] == 0x8087) {
				tbt_mode = j;
			} else if (vmode->alt_mode_svids[j] == 0xff01) {
				dp_mode = j;
			}
		}

		if (tbt_mode > 0) {
			vmode->desired_alt_mode = tbt_mode;
		} else if (dp_mode > 0) {
			vmode->desired_alt_mode = dp_mode;
		} else {
			vmode->desired_alt_mode = vmode->desired_alt_mode - 1;
		}
	}
}

static int rts5453_init_virtual_mode_info(struct rts5453_device *dev)
{
	struct ucsi_control get_alt_modes = {};
	struct ucsi_control get_capability = {};
	int ret = 0;

	get_alt_modes.command = UCSI_CMD_GET_ALTERNATE_MODES;
	get_capability.command = UCSI_CMD_GET_CAPABILITY;

	for (int i = 0; i < dev->active_port_count; ++i) {
		uint8_t connector = i + 1;
		struct virtual_mode_info *vmode = &dev->per_port_mode_info[i];
		struct ucsiv3_get_capability_data *get_cap_data =
			(struct ucsiv3_get_capability_data *)dev->cmd_buffer;

		vmode->connector = connector;

		/* Get capabilities. */
		get_capability.command_specific[0] = connector;

		ret = rts5453_smbus_command(
			dev, connector, SC_UCSI_COMMANDS,
			(uint8_t *)&get_capability, sizeof(get_capability),
			dev->cmd_buffer,
			sizeof(struct ucsiv3_get_capability_data));
		if (ret < 0) {
			ELOG("Failed to get capability on port %u", connector);
			return ret;
		}

		vmode->num_alt_modes = (uint8_t)get_cap_data->num_alt_modes;
		if (vmode->num_alt_modes > MAX_NUM_ALT_MODES) {
			ELOG("Connector has too many alt modes: %d",
			     vmode->num_alt_modes);
			return -1;
		}
		vmode->current_alt_mode = 0xff;
		vmode->allow_usb4 = true;

		/* Fill SVIDs. */
		for (int j = 0; j < vmode->num_alt_modes; ++j) {
			platform_memset(get_alt_modes.command_specific, 0,
					sizeof(get_alt_modes.command_specific));

			/* Recipient = Connector */
			get_alt_modes.command_specific[0] = 0;
			get_alt_modes.command_specific[1] = connector;
			/* alt-mode offset */
			get_alt_modes.command_specific[2] = j;

			ret = rts5453_smbus_command(dev, connector,
						    SC_UCSI_COMMANDS,
						    (uint8_t *)&get_alt_modes,
						    sizeof(get_alt_modes),
						    dev->cmd_buffer, 6);

			if (ret < 0) {
				ELOG("Failed to get altmode; port %u offset %u",
				     connector, j);
				return ret;
			}

			vmode->alt_mode_svids[j] = dev->cmd_buffer[0] |
						   (dev->cmd_buffer[1] << 8);
		}
	}

	rts5453_reset_desired_altmodes(dev);

	return ret;
}

static int rts5453_handle_get_current_cam(struct rts5453_device *dev,
					  uint8_t connector,
					  uint8_t *lpm_data_out)
{
	if (connector > dev->active_port_count) {
		ELOG("Bad connector %u", connector);
		return -1;
	}

	struct virtual_mode_info *vmode =
		&dev->per_port_mode_info[connector - 1];
	lpm_data_out[0] = vmode->current_alt_mode;

	DLOG("[%u]: CAM = 0x%x (desired=0x%x)", connector,
	     vmode->current_alt_mode, vmode->desired_alt_mode);
	return 1;
}

static int rts5453_handle_set_new_cam(struct rts5453_device *dev,
				      uint8_t connector, bool enter,
				      uint8_t new_cam, uint8_t *lpm_data_out)
{
	if (connector > dev->active_port_count) {
		ELOG("Bad connector %u", connector);
		return -1;
	}

	struct virtual_mode_info *vmode =
		&dev->per_port_mode_info[connector - 1];

	if (!(new_cam < vmode->num_alt_modes || new_cam == 0xFF)) {
		ELOG("Invalid new_cam 0x%02x on port %u", new_cam, connector);
		return -1;
	}

	if (new_cam == 0xFF) {
		vmode->current_alt_mode = 0xFF;
		vmode->desired_alt_mode = 0xFF;
		vmode->constat_mask = CONSTAT_SUPPORTED_CAM_CHANGE;
	} else if (enter) {
		vmode->current_alt_mode = new_cam;
		vmode->constat_mask = CONSTAT_SUPPORTED_CAM_CHANGE;
	} else if (!enter && new_cam == vmode->current_alt_mode) {
		vmode->current_alt_mode = 0xff;
		vmode->constat_mask = CONSTAT_SUPPORTED_CAM_CHANGE;
	} else {
		return -1;
	}

	DLOG("[%u]: SET_NEW_CAM with new_cam = 0x%x, enter = %u", connector,
	     new_cam, enter);

	if (vmode->constat_mask) {
		platform_mutex_lock(dev->action_data.lock);
		dev->action_data.action = ACTION_LPM_ALERT;
		dev->action_data.alert_action.connector = connector;
		platform_condvar_signal(dev->action_data.condvar);
		platform_mutex_unlock(dev->action_data.lock);
	}

	return 0;
}

static void rts5453_update_alt_mode_on_connection(struct rts5453_device *dev,
						  uint8_t connector,
						  bool connected)
{
	if (connector > dev->active_port_count) {
		ELOG("Bad connector %u", connector);
		return;
	}

	struct virtual_mode_info *vmode =
		&dev->per_port_mode_info[connector - 1];

	if (!connected) {
		vmode->current_alt_mode = 0xFF;
	} else {
		vmode->current_alt_mode = vmode->desired_alt_mode;
	}

	DLOG("[%u] is %s. Alt-mode = 0x%x", connector,
	     (connected ? "connected" : "disconnected"),
	     vmode->current_alt_mode);
}

/* Call with dev->cmd_buffer already set. */
static int rts5453_set_notification_per_port(struct rts5453_device *dev,
					     uint8_t *lpm_data_out)
{
	int ret = 0;
	int cmd = SC_SET_NOTIFICATION_ENABLE;
	uint8_t data_size = 4;

	/* Print out what bits are being set in notifications */
	uint32_t *enable_bits = ((uint32_t *)&dev->cmd_buffer[2]);
	DLOG("SET_NOTIFICATION_ENABLE with bits = 0x%04x", *enable_bits);

	for (uint8_t port = dev->active_port_count; port > 0; --port) {
		dev->cmd_buffer[1] = 0; /* fixed port-num = 0 */
		ret = rts5453_smbus_command(dev, port, cmd, dev->cmd_buffer,
					    data_size + 2, lpm_data_out,
					    SMBUS_MAX_BLOCK_SIZE);

		if (ret < 0) {
			ELOG("Failed to set notification on port %d", port);
			return ret;
		}
	}

	return ret;
}

static int rts5453_ucsi_execute_cmd(struct ucsi_pd_device *device,
				    struct ucsi_control *control,
				    uint8_t *lpm_data_out)
{
	struct rts5453_device *dev = CAST_FROM(device);
	uint8_t ucsi_command = control->command;
	int cmd;
	/* Data size skips command, write size, sub-cmd and port-num.
	 * When writing via rts5453_smbus_command, we always add 2 to data_size
	 * (for sub-cmd and port-num).
	 */
	uint8_t data_size = 0;
	uint8_t port_num = RTS_DEFAULT_PORT;
	int ret = 0;

	if (control->command == 0 || control->command > UCSI_CMD_VENDOR_CMD) {
		ELOG("Invalid command 0x%x", control->command);
		return -1;
	}

	switch (ucsi_command) {
	/* The following UCSI commands change the port being addressed.
	 * These commands have the connector number at offset 16.
	 */
	case UCSI_CMD_CONNECTOR_RESET:
	case UCSI_CMD_GET_CONNECTOR_CAPABILITY:
	case UCSI_CMD_GET_CAM_SUPPORTED:
	case UCSI_CMD_GET_CURRENT_CAM:
	case UCSI_CMD_SET_NEW_CAM:
	case UCSI_CMD_GET_PDOS:
	case UCSI_CMD_GET_CABLE_PROPERTY:
	case UCSI_CMD_GET_CONNECTOR_STATUS:
	case UCSI_CMD_GET_ERROR_STATUS:
	case UCSI_CMD_GET_PD_MESSAGE:
	case UCSI_CMD_GET_ATTENTION_VDO:
	case UCSI_CMD_GET_CAM_CS:
		port_num = UCSI_7BIT_PORTMASK(control->command_specific[0]);
		break;

	/* The following UCSI commands change the port being addressed.
	 * These commands have the connector number at offset 24.
	 */
	case UCSI_CMD_GET_ALTERNATE_MODES:
		port_num = UCSI_7BIT_PORTMASK(control->command_specific[1]);
		break;
	}

	switch (ucsi_command) {
	case UCSI_CMD_ACK_CC_CI:
		struct ucsiv3_ack_cc_ci_cmd *ack_cmd =
			(struct ucsiv3_ack_cc_ci_cmd *)control->command_specific;
		struct ucsiv3_get_connector_status_data *next_connector_status =
			NULL;

		bool has_pending_ci = dev->ppm->get_next_connector_status(
			dev->ppm->dev, &port_num, &next_connector_status);
		cmd = SC_ACK_CC_CI;
		data_size = 5;
		platform_memset(dev->cmd_buffer, 0, data_size + 2);

		/* Already memset but for reference:
		 * dev->cmd_buffer[0] = 0;    # Reserved and 0.
		 * dev->cmd_buffer[1] = 0x0;  # port fixed to 0.
		 */

		/* Acking on a command or async event? */
		if (ack_cmd->command_complete_ack) {
			/* Command completed acknowledge */
			dev->cmd_buffer[6] = 0x1;
		}

		/* TODO - Do we clear all events on this ack or do we expect OPM
		 * to need a separate notification PER event. I think the answer
		 * is single ack -- double check and clear this comment.
		 */
		else if (ack_cmd->connector_change_ack && has_pending_ci) {
			/* Copy UCSI status change bits and leave RTK bits alone
			 * (4, 5)
			 */
			uint16_t mask =
				next_connector_status->connector_status_change;
			/* port_num affects chip addressing */
			dev->cmd_buffer[1] = 0;
			dev->cmd_buffer[2] = mask & 0xff;
			/* Always clear RTK bits (we don't use it in UCSI) */
			dev->cmd_buffer[3] = (mask >> 8) & 0xff;
			dev->cmd_buffer[4] = 0xff;
			dev->cmd_buffer[5] = 0xff;

			/* Clear any constat mask on virtual mode. */
			if (port_num && port_num < dev->active_port_count) {
				dev->per_port_mode_info[port_num - 1]
					.constat_mask = 0;
			}

			DLOG("ACK_CC_CI with mask (UCSI 0x%x), RTK [%02x, %02x, %02x, %02x] "
			     "on port %d",
			     mask, dev->cmd_buffer[2], dev->cmd_buffer[3],
			     dev->cmd_buffer[4], dev->cmd_buffer[5], port_num);
		} else {
			ELOG("Ack invalid. Ack byte (0x%x), Has pending Connector "
			     "Indication(%b)",
			     control->command_specific[0], has_pending_ci);
			return -1;
		}

		break;
	case UCSI_CMD_SET_NOTIFICATION_ENABLE:
		cmd = SC_SET_NOTIFICATION_ENABLE;
		data_size = 4;
		platform_memset(dev->cmd_buffer, 0, data_size + 2);
		dev->cmd_buffer[0] = 0x1; /* sub-cmd */
		dev->cmd_buffer[1] = 0x0; /* fixed port-num = 0 */

		platform_memcpy(&dev->cmd_buffer[2], control->command_specific,
				data_size);
		break;

	case UCSI_CMD_GET_PD_MESSAGE:
		/* The Realtek PDC does not support GET_PD_MESSAGE, but it can
		 * return SOP/SOP' identity with GET_VDO. If the GET_PD_MESSAGE
		 * request is for the discover identity response, map it to the
		 * corresponding GET_VDO command.
		 */
		struct ucsiv3_get_pd_message_cmd *get_pd_message_cmd =
			(struct ucsiv3_get_pd_message_cmd *)
				control->command_specific;

		if (get_pd_message_cmd->response_message_type != 4) {
			ELOG("Unsupported Response Message type in GET_PD_MESSAGE: %d",
			     get_pd_message_cmd->response_message_type);
			return -1;
		}

		cmd = SC_GET_VDO;
		data_size = 8;
		platform_memset(dev->cmd_buffer, 0, data_size + 2);
		/* GET_VDO sub command */
		dev->cmd_buffer[0] = 0x9A;
		/* Fixed port-num = 0 */
		dev->cmd_buffer[1] = 0x00;
		/* Recipient | Num VDOs (7) */
		dev->cmd_buffer[2] = (get_pd_message_cmd->recipient << 3) | 7;
		/* VDOs in the Discover identity response. GET_PD_MESSAGE
		 * also returns the VDM header, so cmd_buffer[3] requests a
		 * reserved value as a placeholder. cmd_buffer[4] through
		 * cmd_buffer[9] request the ID header VDO, Cert Stat VDO,
		 * and Product VDO followed by Product Type VDOs 1-3.
		 */
		dev->cmd_buffer[3] = 0x00;
		dev->cmd_buffer[4] = 0x01;
		dev->cmd_buffer[5] = 0x02;
		dev->cmd_buffer[6] = 0x03;
		dev->cmd_buffer[7] = 0x04;
		dev->cmd_buffer[8] = 0x05;
		dev->cmd_buffer[9] = 0x06;
		break;

	case UCSI_CMD_GET_CURRENT_CAM:
		return rts5453_handle_get_current_cam(dev, port_num,
						      lpm_data_out);
	case UCSI_CMD_SET_NEW_CAM:
		bool enter = !!(control->command_specific[0] & 0x80);
		uint8_t new_cam = control->command_specific[1];

		return rts5453_handle_set_new_cam(dev, port_num, enter, new_cam,
						  lpm_data_out);
	default:
		/* For most UCSI commands, just set the cmd = 0x0E and copy the
		 * additional data from the command to smbus output.
		 */
		cmd = SC_UCSI_COMMANDS;
		data_size = ucsi_commands[ucsi_command].command_copy_length;
		platform_memset(dev->cmd_buffer, 0, data_size + 2);
		dev->cmd_buffer[0] = ucsi_command;
		dev->cmd_buffer[1] = data_size;

		/* Seems like developer error here. We only support up to 6
		 * bytes.
		 */
		if (data_size > 6) {
			ELOG("UCSI commands using MESSAGE_OUT are unsupported."
			     "Given data_size was %d",
			     data_size);
			return -1;
		}
		/* Copy any command data */
		else if (data_size > 0) {
			platform_memcpy(&dev->cmd_buffer[2],
					control->command_specific, data_size);
		}

		break;
	}

	/* Note special behavior for SET_NOTIFICATION_ENABLE. */
	if (ucsi_command == UCSI_CMD_SET_NOTIFICATION_ENABLE) {
		return rts5453_set_notification_per_port(dev, lpm_data_out);
	}

	ret = rts5453_smbus_command(dev, port_num, cmd, dev->cmd_buffer,
				    data_size + 2, lpm_data_out,
				    SMBUS_MAX_BLOCK_SIZE);

	if (ret >= 0) {
		/* Successful PPM reset will reset desired alt-mode. */
		if (ucsi_command == UCSI_CMD_PPM_RESET) {
			rts5453_reset_desired_altmodes(dev);
		} else if (ucsi_command == UCSI_CMD_GET_CONNECTOR_STATUS) {
			struct ucsiv3_get_connector_status_data *constat =
				(struct ucsiv3_get_connector_status_data *)
					lpm_data_out;

			uint16_t constat_change =
				constat->connector_status_change;
			if (constat_change & ((1 << 14) | (1 << 11))) {
				rts5453_update_alt_mode_on_connection(
					dev, port_num, constat->connect_status);
			}

			if (port_num && port_num < dev->active_port_count) {
				constat->connector_status_change |=
					dev->per_port_mode_info[port_num - 1]
						.constat_mask;
			}
		} else if (ucsi_command == UCSI_CMD_GET_CAPABILITY) {
			struct ucsiv3_get_capability_data *cap =
				(struct ucsiv3_get_capability_data *)
					lpm_data_out;

			/* Force enable Alt-mode details supported and
			 * Alt-mode override supported.
			 */
			cap->optional_features |= ((1 << 2) | (1 << 3));

			/* Force enable GET_PD_MESSAGE support. */
			cap->optional_features |= (1 << 8);
		}
	}

	return ret;
}

static int rts5453_ucsi_get_active_port_count(struct ucsi_pd_device *device)
{
	struct rts5453_device *dev = CAST_FROM(device);

	return dev->active_port_count;
}

int rts5453_vendor_cmd_internal(struct rts5453_device *dev, uint8_t port,
				uint8_t enable_bits)
{
	uint8_t cmd[] = { /*0x3,*/ 0xda, 0x0b, enable_bits };
	return rts5453_smbus_command(dev, port, SC_VENDOR_CMD, cmd,
				     ARRAY_SIZE(cmd), NULL, 0);
}

int rts5453_vendor_cmd_disable(struct rts5453_device *dev, uint8_t port)
{
	return rts5453_vendor_cmd_internal(dev, port, 0);
}

int rts5453_vendor_cmd_enable_smbus(struct rts5453_device *dev, uint8_t port)
{
	return rts5453_vendor_cmd_internal(dev, port, 0x1);
}

int rts5453_vendor_cmd_enable_smbus_flash_access(struct rts5453_device *dev,
						 uint8_t port)
{
	return rts5453_vendor_cmd_internal(dev, port, 0x3);
}

int rts5453_set_flash_protection(struct rts5453_device *dev, int flash_protect)
{
	uint8_t cmd[] = { /*0x1,*/ flash_protect ? 0x1 : 0x0 };
	return rts5453_smbus_command(dev, RTS_DEFAULT_PORT, SC_SET_SPI_PROTECT,
				     cmd, ARRAY_SIZE(cmd), NULL, 0);
}

int rts5453_isp_validation(struct rts5453_device *dev)
{
	uint8_t cmd[] = { /*0x1,*/ 0x1 };
	return rts5453_smbus_command(dev, RTS_DEFAULT_PORT, SC_ISP_VALIDATION,
				     cmd, ARRAY_SIZE(cmd), NULL, 0);
}

int rts5453_reset_to_flash(struct rts5453_device *dev)
{
	uint8_t cmd[] = { /*0x3,*/ 0xDA, 0x0B, 0x01 };
	return rts5453_smbus_command(dev, RTS_DEFAULT_PORT, SC_RESET_TO_FLASH,
				     cmd, ARRAY_SIZE(cmd), NULL, 0);
}

int rts5453_write_to_flash(struct rts5453_device *dev, int flash_bank,
			   const char *inbuf, uint8_t size, size_t offset)
{
	int flash_cmd = SC_WRITE_FLASH_0K_64K;
	uint8_t cmd[SMBUS_MAX_BLOCK_SIZE];
	uint16_t addr_h = 0;
	uint16_t addr_l = 0;

	/* Bounds check */
	int start = RTS5453_BANK0_START + offset;
	int end = RTS5453_BANK0_END;
	if (flash_bank != 0) {
		start = RTS5453_BANK1_START + offset;
		end = RTS5453_BANK1_END;
	}

	/* Get addr_h and addr_l */
	addr_h = (uint16_t)((start >> 16) & 0xFFFF);
	addr_l = (uint16_t)(start & 0xFFFF);

	/* Limited by smbus block size */
	if (size > FW_BLOCK_CHUNK_SIZE) {
		ELOG("Can't write with size=%d > max smbus size=%d", size,
		     FW_BLOCK_CHUNK_SIZE);
		return -1;
	}

	/* We can't write more than flash exists */
	if (start + size > end) {
		ELOG("Write to flash exceeds bounds of flash: bank %d, start(0x%x), "
		     "size(0x%x), end(0x%x)",
		     flash_bank, start, size, end);
		return -1;
	}

	/* Determine which smbus write command to use. */
	switch (addr_h) {
	case 3:
		flash_cmd = SC_WRITE_FLASH_192K_256K;
		break;
	case 2:
		flash_cmd = SC_WRITE_FLASH_128K_192K;
		break;
	case 1:
		flash_cmd = SC_WRITE_FLASH_64K_128K;
		break;
	case 0:
		flash_cmd = SC_WRITE_FLASH_0K_64K;
		break;
	case 4:
	default:
		ELOG("Addr_h %d is out of bounds", addr_h);
		return -1;
	}

	/* Build the command.
	 * cmd[0] = ADDR_L
	 * cmd[1] = ADDR_H
	 * cmd[2] = write size
	 */

	cmd[0] = (uint8_t)(addr_l & 0xFF);
	cmd[1] = (uint8_t)((addr_l >> 8) & 0xFF);
	cmd[2] = size;
	platform_memcpy(&cmd[3], inbuf, size);

	size = size + 3;

	return rts5453_smbus_command(dev, RTS_DEFAULT_PORT, flash_cmd, cmd,
				     size, NULL, 0);
}

int rts5453_get_ic_status(struct rts5453_device *dev,
			  struct rts5453_ic_status *status)
{
	uint8_t cmd[] = { /*0x3,*/ 0x0, 0x0, 0x1F };
	uint8_t out[SMBUS_MAX_BLOCK_SIZE];
	platform_memset(out, 0, SMBUS_MAX_BLOCK_SIZE);

	if (!status) {
		return -1;
	}

	int ret = rts5453_smbus_command(dev, RTS_DEFAULT_PORT, SC_GET_IC_STATUS,
					cmd, ARRAY_SIZE(cmd), out,
					SMBUS_MAX_BLOCK_SIZE);

	DLOG("Smbus command returned: %d", ret);
	DLOG_START("Raw value: [");
	for (int i = 0; i < SMBUS_MAX_BLOCK_SIZE; ++i) {
		DLOG_LOOP("0x%02x, ", out[i]);
	}
	DLOG_END("]");

	if (ret == 31) {
		platform_memcpy((void *)status, out, 31);
	}

	return ret;
}

static int rts5453_ppm_reset(struct rts5453_device *dev, uint8_t port)
{
	uint8_t cmd[] = { /* 0x0e , */ 0x01, 0x00 };
	uint8_t unused_out[SMBUS_MAX_BLOCK_SIZE];

	return rts5453_smbus_command(dev, port, SC_UCSI_COMMANDS, cmd,
				     ARRAY_SIZE(cmd), unused_out,
				     SMBUS_MAX_BLOCK_SIZE);
}

static int rts5453_set_notification_enable(struct rts5453_device *dev,
					   uint8_t port, uint32_t mask)
{
	uint8_t cmd[] = { /* 0x06 , */
			  0x01,
			  0x00,
			  mask & 0xFF,
			  (mask >> 8) && 0xFF,
			  (mask >> 16) & 0xFF,
			  (mask >> 24) & 0xff
	};

	return rts5453_smbus_command(dev, port, SC_SET_NOTIFICATION_ENABLE, cmd,
				     ARRAY_SIZE(cmd), NULL, 0);
}

static int rts5453_get_capabilities(struct rts5453_device *dev, uint8_t *out,
				    size_t size)
{
	uint8_t cmd[] = { /* 0x02, */ 0x06, 0x00 };

	return rts5453_smbus_command(dev, RTS_DEFAULT_PORT, SC_UCSI_COMMANDS,
				     cmd, ARRAY_SIZE(cmd), out, size);
}

static void rts5453_ucsi_cleanup(struct ucsi_pd_driver *driver)
{
	if (driver->dev) {
		struct rts5453_device *dev = CAST_FROM(driver->dev);

		/* Clean up PPM first AND then smbus. */
		if (dev->ppm) {
			dev->ppm->cleanup(dev->ppm);
			platform_free(dev->ppm);
		}

		if (dev->smbus) {
			dev->smbus->cleanup(dev->smbus);

			/* If there was an interrupt task, it will end when
			 * SMBUS is cleaned up.
			 */
			if (dev->lpm_interrupt_task) {
				platform_task_complete(dev->lpm_interrupt_task);
			}

			platform_free(dev->smbus);
		}

		platform_free(driver->dev);
		driver->dev = NULL;
	}
}

#define ALERT_RECEIVING_ADDRESS 0xC

/* Query ARA (alert receiving address) and forward as lpm_id to PPM. If we
 * received an alert on an unexpected address, raise an error.
 */
static int rts5453_ucsi_handle_interrupt(struct rts5453_device *dev)
{
	const struct pd_driver_config *config = dev->driver_config;
	uint8_t port_id = 0;
	uint8_t ara_address;

	int ret =
		dev->smbus->read_ara(dev->smbus->dev, ALERT_RECEIVING_ADDRESS);
	if (ret < 0) {
		return -1;
	}

	ara_address = ret & 0xff;
	for (int i = 0; i < config->max_num_ports; ++i) {
		if (ara_address == config->port_address_map[i]) {
			port_id = i + 1;
			break;
		}
	}

	/* If we got a valid port (one we expected), send LPM alert to PPM. */
	if (port_id > 0) {
		dev->ppm->lpm_alert(dev->ppm->dev, port_id);
	} else {
		ELOG("Alerted by unexpected chip: 0x%x", ara_address);
	}

	return port_id > 0 ? 0 : -1;
}

static void rts5453_lpm_irq_task(void *context)
{
	struct rts5453_device *dev = CAST_FROM(context);
	struct smbus_driver *smbus = dev->smbus;

	DLOG("LPM IRQ task started");
	while (smbus->block_for_interrupt(smbus->dev) != -1) {
		rts5453_ucsi_handle_interrupt(dev);
	}

	ELOG("LPM IRQ task ended. This is fatal.");
}

static int rts5453_ucsi_configure_lpm_irq(struct ucsi_pd_device *device)
{
	struct rts5453_device *dev = CAST_FROM(device);

	if (dev->lpm_interrupt_task != NULL) {
		return 0;
	}

	if (platform_task_init(rts5453_lpm_irq_task, dev,
			       &dev->lpm_interrupt_task)) {
		return -1;
	}

	return 0;
}

static void rts5453_action_task(void *context)
{
	struct rts5453_device *dev = CAST_FROM(context);
	struct driver_action_t *actions = &dev->action_data;

	platform_mutex_lock(actions->lock);
	while (actions->action != ACTION_CLEANUP_EXIT) {
		platform_condvar_wait(actions->condvar, actions->lock);

		switch (actions->action) {
		case ACTION_LPM_ALERT:
			dev->ppm->lpm_alert(dev->ppm->dev,
					    actions->alert_action.connector);
			break;
		default:
			DLOG("Task signaled for unhandled action %u",
			     actions->action);
			break;
		}

		/* Clear action for next iteration. */
		actions->action = ACTION_NONE;
	}
	platform_mutex_unlock(actions->lock);
	platform_task_exit();
}

static int rts5453_ucsi_init_ppm(struct ucsi_pd_device *device)
{
	struct rts5453_device *dev = CAST_FROM(device);
	uint8_t caps[16];
	int bytes_read;
	uint8_t num_ports = 0;
	uint8_t max_num_ports = dev->driver_config->max_num_ports;

	/* Init flow for RTS5453:
	 * - First run VENDOR_CMD_ENABLE
	 * - SET NOTIFICATION to very basic set to set to IDLE mode.
	 * - PPM reset.
	 * - Get capability to get number of ports (necessary for handling
	 *   notifications and correct setting CCI). This may not match max num
	 *   ports if firmware doesn't enable all ports that driver config has.
	 */

	for (uint8_t port = 1; port <= max_num_ports; ++port) {
		if (rts5453_vendor_cmd_enable_smbus(dev, port) == -1) {
			ELOG("Failed in PPM_INIT: enable vendor commands");
			return -1;
		}

		if (rts5453_ppm_reset(dev, port) == -1) {
			ELOG("Failed in PPM_INIT: ppm reset");
			return -1;
		}

		if (rts5453_set_notification_enable(dev, port, 0x0) == -1) {
			ELOG("Failed in PPM_INIT: clear notifications enabled");
			return -1;
		}
	}

	bytes_read = rts5453_get_capabilities(dev, caps, 16);
	if (bytes_read == -1 || bytes_read < 16) {
		ELOG("Failed in PPM_INIT: get_capabilities returned %d",
		     bytes_read);
		DLOG_START("Capabilities bytes: [");
		for (int i = 0; i < bytes_read; ++i) {
			DLOG_LOOP("0x%x, ", caps[i]);
		}
		DLOG_END("]");
		return -1;
	}

	num_ports = caps[4];

	/* Limit the number of ports to maximum configured number of ports. */
	if (num_ports > max_num_ports) {
		ELOG("Truncated number of ports from %d to %d", num_ports,
		     max_num_ports);
		num_ports = max_num_ports;
	}

	dev->active_port_count = num_ports;

	/* Initialize per port virtual modes. */
	dev->per_port_mode_info =
		platform_calloc(num_ports, sizeof(*dev->per_port_mode_info));

	if (rts5453_init_virtual_mode_info(dev) < 0) {
		ELOG("Failed to set up virtual altmodes!");
		return -1;
	}

	/* Initialize actions task. */
	dev->action_data.lock = platform_mutex_init();
	if (!dev->action_data.lock) {
		return -1;
	}

	dev->action_data.condvar = platform_condvar_init();
	if (!dev->action_data.condvar) {
		return -1;
	}

	dev->action_task = platform_task_init(rts5453_action_task, dev);
	if (dev->action_task == NULL) {
		return -1;
	}

	DLOG("RTS5453 PPM is ready to init.");
	return dev->ppm->init_and_wait(dev->ppm->dev, num_ports);
}

static struct ucsi_ppm_driver *
rts5453_ucsi_get_ppm(struct ucsi_pd_device *device)
{
	struct rts5453_device *dev = CAST_FROM(device);
	return dev->ppm;
}

struct ucsi_pd_driver *rts5453_open(struct smbus_driver *smbus,
				    struct pd_driver_config *config)
{
	struct rts5453_device *dev = NULL;
	struct ucsi_pd_driver *drv = NULL;

	dev = platform_calloc(1, sizeof(struct rts5453_device));
	if (!dev) {
		goto handle_error;
	}

	dev->smbus = smbus;
	dev->driver_config = config;

	/* Until we init PPM, accept maximum num ports as active. */
	dev->active_port_count = config->max_num_ports;

	drv = platform_calloc(1, sizeof(struct ucsi_pd_driver));
	if (!drv) {
		goto handle_error;
	}

	drv->dev = (struct ucsi_pd_device *)dev;

	drv->configure_lpm_irq = rts5453_ucsi_configure_lpm_irq;
	drv->init_ppm = rts5453_ucsi_init_ppm;
	drv->get_ppm = rts5453_ucsi_get_ppm;
	drv->execute_cmd = rts5453_ucsi_execute_cmd;
	drv->get_active_port_count = rts5453_ucsi_get_active_port_count;
	drv->cleanup = rts5453_ucsi_cleanup;

	/* Initialize the PPM. */
	dev->ppm = ppm_open(drv);
	if (!dev->ppm) {
		ELOG("Failed to open PPM");
		goto handle_error;
	}

	return drv;

handle_error:
	if (dev && dev->ppm) {
		dev->ppm->cleanup(dev->ppm);
		dev->ppm = NULL;
	}

	platform_free(dev);
	platform_free(drv);

	return NULL;
}

struct pd_driver_config rts5453_get_driver_config()
{
	struct pd_driver_config config = {
		.max_num_ports = 2,
		.port_address_map = {
			0x67,
			0x68,
		},
	};

	return config;
}
