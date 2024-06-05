/* Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PD chip UCSI
 * See https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/usb-type-c-ucsi-spec.pdf
 *
 */

#include <atomic.h>
#include "chipset.h"
#include "config.h"
#include "customized_shared_memory.h"
#include "cypress_pd_common.h"
#include "timer.h"
#include "ucsi.h"
#include "hooks.h"
#include "string.h"
#include "console.h"
#include "task.h"
#include "util.h"
#include "zephyr_console_shim.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define CCI_NOT_SUPPORTED_FLAG BIT(25)
#define CCI_CANCELED_FLAG BIT(26)
#define CCI_RESET_FLAG BIT(27)
#define CCI_BUSY_FLAG BIT(28)
#define CCI_ACKNOWLEDGE_FLAG BIT(29)
#define CCI_ERROR_FLAG BIT(30)
#define CCI_COMPLETE_FLAG BIT(31)

static struct pd_chip_ucsi_info_t pd_chip_ucsi_info[] = {
	[PD_CHIP_0] = {

	},
	[PD_CHIP_1] = {

	}
};

static int ucsi_debug_enable;
static uint8_t s0ix_connector_change_indicator;
static bool read_complete;

void ucsi_set_debug(bool enable)
{
	ucsi_debug_enable = enable;
}

timestamp_t ucsi_wait_time;

void ucsi_set_next_poll(uint32_t from_now_us)
{
	timestamp_t now = get_time();

	ucsi_wait_time.val = now.val + from_now_us;
}

const char *command_names(uint8_t command)
{
	static const char *response_codes[0x14] = {
		"RESERVE",
		"PPM_RESET",
		"CANCEL",
		"CONNECTOR_RESET",
		"ACK_CC_CI",
		"SET_NOTIFICATION_ENABLE",
		"GET_CAPABILITY",
		"GET_CONNECTOR_CAPABILITY",
		"SET_UOM",
		"SET_UOR",
		"SET_PDM",
		"SET_PDR",
		"GET_ALTERNATE_MODES",
		"GET_CAM_SUPPORTED",
		"GET_CURRENT_CAM",
		"SET_NEW_CAM",
		"GET_PDOS",
		"GET_CABLE_PROPERTY",
		"GET_CONNECTOR_STATUS",
		"GET_ERROR_STATUS"
	};
	if (command < 0x14) {
		return response_codes[command];
	}
	return "";
}

int ucsi_write_tunnel(void)
{
	uint8_t *message_out = host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_MESSAGE_OUT);
	uint8_t *command = host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_COMMAND);
	uint8_t change_connector_indicator = 0;
	int i;
	int offset = 0;
	int rv = EC_SUCCESS;

	/**
	 * Note that CONTROL data has always to be written after MESSAGE_OUT data is written
	 * A write to CONTROL (in CCGX) triggers processing of that command.
	 * Hence MESSAGE_OUT must be available before CONTROL is written to CCGX.
	 */

	if (*command == UCSI_CMD_PPM_RESET) {
		cypd_usci_ppm_reset();
		CPRINTS("UCSI PPM_RESET");
	}

	pd_chip_ucsi_info[0].read_tunnel_complete = 0;
	pd_chip_ucsi_info[1].read_tunnel_complete = 0;

	switch (*command) {
	case UCSI_CMD_GET_CONNECTOR_STATUS:
	case UCSI_CMD_GET_CONNECTOR_CAPABILITY:
	case UCSI_CMD_CONNECTOR_RESET:
	case UCSI_CMD_SET_UOM:
	case UCSI_CMD_SET_UOR:
	case UCSI_CMD_SET_PDR:
	case UCSI_CMD_GET_CAM_SUPPORTED:
	case UCSI_CMD_SET_NEW_CAM:
	case UCSI_CMD_GET_PDOS:
	case UCSI_CMD_GET_CABLE_PROPERTY:
	case UCSI_CMD_GET_ALTERNATE_MODES:
	case UCSI_CMD_GET_CURRENT_CAM:

		if (*command == UCSI_CMD_GET_ALTERNATE_MODES) {
			/**
			 * Workaround: PD chip cannot process the SOP/SOP'/SOP'' alternate mode
			 * event, ignore the recipient field.
			 */
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_CTR_SPECIFIC) =
				(*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_CTR_SPECIFIC) & 0xFC);
			offset = 1;
		}

		/**
		 * those command will control specific pd port,
		 * so we need to check the command connector number.
		 */
		change_connector_indicator =
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_CTR_SPECIFIC + offset) & 0x7f;

		if (change_connector_indicator > 0x02) {
			/*
			 * Port 3 (b011) should be controller 1 UCSI port 1
			 * Port 4 (b100) should be controller 1 UCSI port 2
			 */
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_CTR_SPECIFIC + offset) =
				((*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_CTR_SPECIFIC + offset)
				& 0x80) | (change_connector_indicator >> 1));
			i = 1;
		} else
			i = 0;

		pd_chip_ucsi_info[i].wait_ack = 1;
		rv = cypd_write_reg_block(i, CCG_MESSAGE_OUT_REG, message_out, 16);
		rv = cypd_write_reg_block(i, CCG_CONTROL_REG, command, 8);
		break;
	default:
		for (i = 0; i < PD_CHIP_COUNT; i++) {

			/**
			 * If the controller does not needs to respond ACK,
			 * set the read tunnel complete flag.
			 * Because ec will not write the command to PD chips.
			 */
			if (*command == UCSI_CMD_ACK_CC_CI &&
			    pd_chip_ucsi_info[i].wait_ack == 0) {
				pd_chip_ucsi_info[i].read_tunnel_complete = 1;
				continue;
			}

			rv = cypd_write_reg_block(i, CCG_MESSAGE_OUT_REG, message_out, 16);
			if (rv != EC_SUCCESS)
				break;

			rv = cypd_write_reg_block(i, CCG_CONTROL_REG, command, 8);
			if (rv != EC_SUCCESS)
				break;

			/**
			 * ACK_CC_CI command is the end of the UCSI command,
			 * does not need to wait ack.
			 */
			if (*command == UCSI_CMD_ACK_CC_CI)
				pd_chip_ucsi_info[i].wait_ack = 0;
			else
				pd_chip_ucsi_info[i].wait_ack = 1;
		}
		break;
	}

	if (ucsi_debug_enable) {
		CPRINTS("UCSI Write P:%d Cmd 0x%016llx %s",
			change_connector_indicator,
			*(uint64_t *)command,
			command_names(*command));
		if (command[1])
			cypd_print_buff("UCSI Msg Out: ", message_out, 6);
	}

	usleep(50);
	return rv;
}

void record_ucsi_connector_change_event(int controller, int port)
{
	int cci_port = (controller << 1) + port + 1;

	if (!chipset_in_state(CHIPSET_STATE_ON) && !chipset_in_state(CHIPSET_STATE_ANY_OFF))
		s0ix_connector_change_indicator |= BIT(cci_port);
}

static void clear_ucsi_connector_change_event(void)
{
	/* UCSI driver will reset PPM, so clear the indicator */
	if (chipset_in_state(CHIPSET_STATE_ON))
		s0ix_connector_change_indicator = 0;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, clear_ucsi_connector_change_event, HOOK_PRIO_DEFAULT);

static void resend_ucsi_connector_change_event(void);
DECLARE_DEFERRED(resend_ucsi_connector_change_event);

static void resend_ucsi_connector_change_event(void)
{
	static int process_port = 1;
	static int resume_flag;

	if (s0ix_connector_change_indicator == 0) {
		process_port = 1;
		resume_flag = 0;
	} else {
		if (!resume_flag) {
			/* Wait host driver ready */
			hook_call_deferred(&resend_ucsi_connector_change_event_data, 500 * MSEC);
			resume_flag = 1;
			return;
		}

		if (s0ix_connector_change_indicator & BIT(process_port)) {
			pd_chip_ucsi_info[(process_port - 1) >> 1].cci =
				(BIT(29) | process_port << 1);

			if (ucsi_debug_enable) {
				uint32_t cci_reg =
					pd_chip_ucsi_info[(process_port - 1) >> 1].cci;

				CPRINTS("Resend: P%d CCI: 0x%08x Port%d, %s%s%s%s%s%s%s",
				(process_port - 1) >> 1,
				cci_reg,
				(cci_reg >> 1) & 0x07F,
				cci_reg & CCI_NOT_SUPPORTED_FLAG ? "Not Support " : "",
				cci_reg & CCI_CANCELED_FLAG ? "Canceled " : "",
				cci_reg & CCI_RESET_FLAG ? "Reset " : "",
				cci_reg & CCI_BUSY_FLAG ? "Busy " : "",
				cci_reg & CCI_ACKNOWLEDGE_FLAG ? "Acknowledge " : "",
				cci_reg & CCI_ERROR_FLAG ? "Error " : "",
				cci_reg & CCI_COMPLETE_FLAG ? "Complete " : ""
				);
			}

			pd_chip_ucsi_info[(process_port - 1) >> 1].read_tunnel_complete = 1;
			read_complete = 1;

			s0ix_connector_change_indicator &= ~BIT(process_port);
		}
		process_port++;
		hook_call_deferred(&resend_ucsi_connector_change_event_data, 150 * MSEC);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, resend_ucsi_connector_change_event, HOOK_PRIO_DEFAULT);

int ucsi_read_tunnel(int controller)
{
	int rv;

	if (ucsi_debug_enable && pd_chip_ucsi_info[controller].read_tunnel_complete == 1 &&
		(pd_chip_ucsi_info[controller].cci & CCI_BUSY_FLAG) == 0) {
		CPRINTS("UCSI Read tunnel but previous read still pending");
	}

	rv = cypd_read_reg_block(controller, CCG_CCI_REG,
		&pd_chip_ucsi_info[controller].cci, 4);

	if (rv != EC_SUCCESS)
		CPRINTS("CCI_REG failed");
	/* we need to offset the pd connector number to correct number */
	if (controller == 1 && (pd_chip_ucsi_info[controller].cci & 0xFE))
		/*
		 * Port 3 (b011) should be controller 1 UCSI port 1 (b001)
		 * Port 4 (b100) should be controller 1 UCSI port 2 (b010)
		 * CCI connector change indicate offset bit 1, so need to add 0x04 (0x2 << 1)
		 */
		pd_chip_ucsi_info[controller].cci += 0x04;

	/* If data length is non zero, then get data */
	if (pd_chip_ucsi_info[controller].cci & 0xFF00) {
		rv = cypd_read_reg_block(controller, CCG_MESSAGE_IN_REG,
			pd_chip_ucsi_info[controller].message_in, 16);

		if (rv != EC_SUCCESS)
			CPRINTS("MESSAGE_IN_REG failed");
	} else {
		memset(pd_chip_ucsi_info[controller].message_in, 0, 16);
	}

	if (ucsi_debug_enable) {
		uint32_t cci_reg = pd_chip_ucsi_info[controller].cci;

		CPRINTS("P%d CCI: 0x%08x Port%d, %s%s%s%s%s%s%s",
			controller,
			cci_reg,
			(cci_reg >> 1) & 0x07F,
			cci_reg & CCI_NOT_SUPPORTED_FLAG ? "Not Support " : "",
			cci_reg & CCI_CANCELED_FLAG ? "Canceled " : "",
			cci_reg & CCI_RESET_FLAG ? "Reset " : "",
			cci_reg & CCI_BUSY_FLAG ? "Busy " : "",
			cci_reg & CCI_ACKNOWLEDGE_FLAG ? "Acknowledge " : "",
			cci_reg & CCI_ERROR_FLAG ? "Error " : "",
			cci_reg & CCI_COMPLETE_FLAG ? "Complete " : ""
			);
		if (cci_reg & 0xFF00) {
			cypd_print_buff("Message ", pd_chip_ucsi_info[controller].message_in, 16);
		}
	}

	/**
	 * When the system enter sleep mode, EC should record the change indicator then resend when
	 * the system resume.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON) && !chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return EC_SUCCESS;

	/**
	 * 1. Ignore the same CCI indicator without any command
	 * 2. Ignore the same CCI indicator with busy flags
	 */
	if (!(memcmp(host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_CONN_CHANGE),
		&pd_chip_ucsi_info[controller].cci, 4)) &&
		((*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_COMMAND) == 0) ||
		*(uint32_t *)host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_CONN_CHANGE)&CCI_BUSY_FLAG))
		return EC_ERROR_UNKNOWN;

	pd_chip_ucsi_info[controller].read_tunnel_complete = 1;

	switch (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_COMMAND)) {
	case UCSI_CMD_PPM_RESET:
	case UCSI_CMD_CANCEL:
	case UCSI_CMD_SET_NOTIFICATION_ENABLE:
	case UCSI_CMD_GET_CAPABILITY:
	case UCSI_CMD_GET_ERROR_STATUS:
		/* Those command need to wait two pd chip to response completed */
		if (pd_chip_ucsi_info[0].read_tunnel_complete &&
		    pd_chip_ucsi_info[1].read_tunnel_complete)
			read_complete = 1;
		else
			read_complete = 0;
		break;
	case UCSI_CMD_ACK_CC_CI:
		if (pd_chip_ucsi_info[0].read_tunnel_complete &&
		    pd_chip_ucsi_info[1].read_tunnel_complete) {
			read_complete = 1;

			/* workaround for linux driver */
			if ((pd_chip_ucsi_info[controller].cci & CCI_ACKNOWLEDGE_FLAG) !=
				pd_chip_ucsi_info[controller].cci)
				pd_chip_ucsi_info[controller].wait_ack = 1;
		} else
			read_complete = 0;
		break;
	default:
		if (pd_chip_ucsi_info[0].read_tunnel_complete ||
		    pd_chip_ucsi_info[1].read_tunnel_complete)
			read_complete = 1;
		else
			read_complete = 0;
		break;
	}

	return EC_SUCCESS;
}

int ucsi_startup(int controller)
{
	int rv = EC_SUCCESS;
	int data;

	ucsi_set_next_poll(0);
	rv = cypd_write_reg8(controller, CCG_UCSI_CONTROL_REG, CYPD_UCSI_START);
	if (rv != EC_SUCCESS)
		CPRINTS("UCSI start command fail!");

	if (cypd_wait_for_ack(controller, 100) != EC_SUCCESS) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}

	rv = cypd_get_int(controller, &data);

	if (data & CCG_DEV_INTR) {
		rv = cypd_read_reg_block(controller, CCG_VERSION_REG,
			&pd_chip_ucsi_info[controller].version, 2);

		if (rv != EC_SUCCESS)
			CPRINTS("UCSI get version fail!");

		memcpy(host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_VERSION),
			&pd_chip_ucsi_info[controller].version, 2);

		cypd_clear_int(controller, CCG_DEV_INTR);
	}
	return rv;
}

/**
 * Suggested by bios team, we don't use host command frequenctly.
 * So we need to polling the flags to get the ucsi event form host.
 */

void check_ucsi_event_from_host(void)
{
	void *message_in;
	uint32_t *cci;
	int i;
	int rv;

	if (read_complete == 0 && !timestamp_expired(ucsi_wait_time, NULL)) {
		if (ucsi_debug_enable)
			CPRINTS("UCSI waiting for time expired");
		return;
	}

	/* If the UCSI interface previously was busy then
	 * poll to see if the busy bit cleared
	 */
	for (i = 0; i < PD_CHIP_COUNT; i++) {
		if (pd_chip_ucsi_info[i].cci & CCI_BUSY_FLAG) {
			ucsi_read_tunnel(i);
		}
	}

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
	    !chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) & UCSI_EVENT)) {

		/**
		 * Following the specification, until the EC reads the VERSION register
		 * from CCGX's UCSI interface, it ignores all writes from the BIOS
		 */
		rv = ucsi_write_tunnel();

		ucsi_set_next_poll(10*MSEC);

		if (rv == EC_ERROR_BUSY)
			return;

		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) &= ~UCSI_EVENT;
		return;
	}

	if (read_complete) {
		if (ucsi_debug_enable) {
			CPRINTS("%s Complete",
				command_names(*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_COMMAND)));
		}
		if (pd_chip_ucsi_info[0].read_tunnel_complete) {
			message_in = pd_chip_ucsi_info[0].message_in;
			cci = &pd_chip_ucsi_info[0].cci;
		}

		if (pd_chip_ucsi_info[1].read_tunnel_complete) {
			message_in = pd_chip_ucsi_info[1].message_in;
			cci = &pd_chip_ucsi_info[1].cci;
		}
		read_complete = false;

		/* Fix UCSI stopping responding to right side ports
		 * the standard says the CCI connector change indicator field
		 * should be 0 for ACK_CC_CI, however our controller responds with
		 * the port number populated for the port with the valid response
		 * so choose this response as a priority when we get an ack from
		 * both controllers
		 */
		if (pd_chip_ucsi_info[1].read_tunnel_complete &&
			pd_chip_ucsi_info[0].read_tunnel_complete) {
			if (pd_chip_ucsi_info[0].cci & 0xFE) {
				message_in = pd_chip_ucsi_info[0].message_in;
				cci = &pd_chip_ucsi_info[0].cci;
			} else if (pd_chip_ucsi_info[1].cci & 0xFE) {
				message_in = pd_chip_ucsi_info[1].message_in;
				cci = &pd_chip_ucsi_info[1].cci;
			}
		}

		if (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_COMMAND) ==
		    UCSI_CMD_GET_CONNECTOR_STATUS &&
		    (((uint8_t *)message_in)[8] & 0x03) > 1) {
			CPRINTS("Overriding Slow charger status");
			/* Override not charging value with nominal charging */
			((uint8_t *)message_in)[8] = (((uint8_t *)message_in)[8] & 0xFC) + 1;
		}

		usleep(2 * MSEC);

		memcpy(host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_MESSAGE_IN), message_in, 16);
		memcpy(host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_CONN_CHANGE), cci, 4);

		/**
		 * TODO: process the two pd results for one response.
		 */

		/* override bNumConnectors to the total number of connectors on the system */
		if (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_COMMAND) == UCSI_CMD_GET_CAPABILITY)
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_MESSAGE_IN + 4) = PD_PORT_COUNT;

		pd_chip_ucsi_info[0].read_tunnel_complete = 0;
		pd_chip_ucsi_info[1].read_tunnel_complete = 0;

		/* clear the UCSI command if busy flag is not set */
		if (0 == (*cci & CCI_BUSY_FLAG)) {
			if (!(*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) & UCSI_EVENT))
				*host_get_memmap(EC_CUSTOMIZED_MEMMAP_UCSI_COMMAND) = 0;
		}
		host_set_single_event(EC_HOST_EVENT_UCSI);
	}
}
