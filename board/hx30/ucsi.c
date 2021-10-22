/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PD chip UCSI 
 */

#include "chipset.h"
#include "config.h"
#include "cypress5525.h"
#include "timer.h"
#include "ucsi.h"
#include "hooks.h"
#include "string.h"
#include "console.h"
#include "task.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

static struct pd_chip_ucsi_info_t pd_chip_ucsi_info[] = {
	[PD_CHIP_0] = {

	},
	[PD_CHIP_1] = {

	}
};

static int ucsi_debug_enable = 0;

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
#ifdef PD_VERBOSE_LOGGING
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
#endif
	return "";
}

static int is_delay;
int ucsi_write_tunnel(void)
{
	uint8_t *message_out = host_get_customer_memmap(EC_MEMMAP_UCSI_MESSAGE_OUT);
	uint8_t *command = host_get_customer_memmap(EC_MEMMAP_UCSI_COMMAND);
	uint8_t change_connector_indicator;
	int i;
	int offset = 0;
	int rv = EC_SUCCESS;

	/**
	 * Note that CONTROL data has always to be written after MESSAGE_OUT data is written
	 * A write to CONTROL (in CCGX) triggers processing of that command.
	 * Hence MESSAGE_OUT must be available before CONTROL is written to CCGX.
	 */
	if (ucsi_debug_enable) {
		CPRINTS("UCSI Write Command 0x%016llx %s",
		*(uint64_t *)command, command_names(*command));
		if (command[1])
			cypd_print_buff("UCSI Msg Out: ", message_out, 6);
	}
	if (*command == UCSI_CMD_PPM_RESET) {
		CPRINTS("UCSI PPM_RESET");
	}

	switch (*command) {
	case UCSI_CMD_GET_CONNECTOR_STATUS:
		/**
		 * try to delay 500 msec to wait PD negotiation complete then send command
		 * to PD chip
		 */
		if (!is_delay) {
			is_delay = 1;
			return EC_ERROR_BUSY;
		}

		CPRINTS("Already delay 500ms, send command to PD chip");
		is_delay = 0;
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

		if (*command == UCSI_CMD_GET_ALTERNATE_MODES)
			offset = 1;

		/** 
		* those command will control specific pd port,
		* so we need to check the command connector number.
		*/
		change_connector_indicator =
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC + offset) & 0x7f;

		if (change_connector_indicator > 0x02) {
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC + offset) =
				((*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC + offset)
				& 0x80) | (change_connector_indicator >> 1));
			i = 1;
		} else
			i = 0;

		pd_chip_ucsi_info[i].write_tunnel_complete = 1;
		rv = cypd_write_reg_block(i, CYP5525_MESSAGE_OUT_REG, message_out, 16);
		rv = cypd_write_reg_block(i, CYP5525_CONTROL_REG, command, 8);
		break;
	default:
		for (i = 0; i < PD_CHIP_COUNT; i++) {

			if (*command == UCSI_CMD_ACK_CC_CI && pd_chip_ucsi_info[i].write_tunnel_complete == 0) {
				pd_chip_ucsi_info[i].read_tunnel_complete = 1;
				continue;
			}

			rv = cypd_write_reg_block(i, CYP5525_MESSAGE_OUT_REG, message_out, 16);
			if (rv != EC_SUCCESS)
				break;

			rv = cypd_write_reg_block(i, CYP5525_CONTROL_REG, command, 8);
			if (rv != EC_SUCCESS)
				break;

			if (*command == UCSI_CMD_ACK_CC_CI)
				pd_chip_ucsi_info[i].write_tunnel_complete = 0;
			else
				pd_chip_ucsi_info[i].write_tunnel_complete = 1;
		}
		break;
	}
	usleep(50);
	return rv;
}

int ucsi_read_tunnel(int controller)
{
	int rv;

	if (ucsi_debug_enable && pd_chip_ucsi_info[controller].read_tunnel_complete == 1) {
		CPRINTS("CYP5525_UCSI Read tunnel but previous read still pending");
	}

	rv = cypd_read_reg_block(controller, CYP5525_CCI_REG,
		&pd_chip_ucsi_info[controller].cci, 4);

	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_CCI_REG failed");
	/* we need to offset the pd connector number to correct number */
	if (controller == 1 && (pd_chip_ucsi_info[controller].cci & 0xFE))
		pd_chip_ucsi_info[controller].cci += 0x04;
	if (pd_chip_ucsi_info[controller].cci & 0xFF00) {
		rv = cypd_read_reg_block(controller, CYP5525_MESSAGE_IN_REG,
			pd_chip_ucsi_info[controller].message_in, 16);

		if (rv != EC_SUCCESS) 
			CPRINTS("CYP5525_MESSAGE_IN_REG failed");
	} else {
		memset(pd_chip_ucsi_info[controller].message_in, 0, 16);
	}

	pd_chip_ucsi_info[controller].read_tunnel_complete = 1;

	if (ucsi_debug_enable) {
		uint32_t cci_reg = pd_chip_ucsi_info[controller].cci;

		CPRINTS("P%d CCI: 0x%08x Port%d, %s%s%s%s%s%s%s",
			controller,
			cci_reg,
			(cci_reg >> 1) & 0x07F,
			cci_reg & BIT(25) ? "Not Support " : "",
			cci_reg & BIT(26) ? "Canceled " : "",
			cci_reg & BIT(27) ? "Reset " : "",
			cci_reg & BIT(28) ? "Busy " : "",
			cci_reg & BIT(29) ? "Acknowledge " : "",
			cci_reg & BIT(30) ? "Error " : "",
			cci_reg & BIT(31) ? "Complete " : ""
			);
		if (cci_reg & 0xFF00) {
			cypd_print_buff("Message ", pd_chip_ucsi_info[controller].message_in, 16);
		}
	}


	return EC_SUCCESS;
}

int cyp5525_ucsi_startup(int controller)
{
	int rv = EC_SUCCESS;
	int data;
	ucsi_set_next_poll(0);
	rv = cypd_write_reg8(controller, CYP5525_UCSI_CONTROL_REG ,CYPD_UCSI_START);
	if (rv != EC_SUCCESS)
		CPRINTS("UCSI start command fail!");

	if (cyp5225_wait_for_ack(controller, 100000) != EC_SUCCESS) {
			CPRINTS("%s timeout on interrupt", __func__);
			return EC_ERROR_INVAL;
	}

	rv = cypd_get_int(controller, &data);

	if (data & CYP5525_DEV_INTR) {
		rv = cypd_read_reg_block(controller, CYP5525_VERSION_REG,
			&pd_chip_ucsi_info[controller].version, 2);

		if (rv != EC_SUCCESS)
			CPRINTS("UCSI start command fail!");

		memcpy(host_get_customer_memmap(EC_MEMMAP_UCSI_VERSION),
			&pd_chip_ucsi_info[controller].version, 2);

		cypd_clear_int(controller, CYP5525_DEV_INTR);
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
	void *cci;
	int read_complete = 0;
	int i;
	int rv;

	if (!timestamp_expired(ucsi_wait_time, NULL)) {
		if (ucsi_debug_enable)
			CPRINTS("UCSI waiting for time expired");
		return;
	}
	for (i = 0; i < PD_CHIP_COUNT; i++) {
		if (pd_chip_ucsi_info[i].cci & BIT(28)) {
			ucsi_read_tunnel(i);
		}
	}

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
			(*host_get_customer_memmap(0x00) & BIT(2))) {

		/**
		 * Following the specification, until the EC reads the VERSION register
		 * from CCGX's UCSI interface, it ignores all writes from the BIOS
		 */
		rv = ucsi_write_tunnel();

		ucsi_set_next_poll(10*MSEC);

		if (rv == EC_ERROR_BUSY)
			return;

		*host_get_customer_memmap(0x00) &= ~BIT(2);
		return;
	}

	switch (*host_get_customer_memmap(EC_MEMMAP_UCSI_COMMAND)) {
	case UCSI_CMD_PPM_RESET:
	case UCSI_CMD_CANCEL:
	case UCSI_CMD_ACK_CC_CI:
	case UCSI_CMD_SET_NOTIFICATION_ENABLE:
	case UCSI_CMD_GET_CAPABILITY:
	case UCSI_CMD_GET_ERROR_STATUS:
		/* Those command need to wait two pd chip to response completed */
		if (pd_chip_ucsi_info[0].read_tunnel_complete && pd_chip_ucsi_info[1].read_tunnel_complete)
			read_complete = 1;
		break;
	default:
		if (pd_chip_ucsi_info[0].read_tunnel_complete || pd_chip_ucsi_info[1].read_tunnel_complete)
			read_complete = 1;
		break;
	}

	if (read_complete) {

		if (pd_chip_ucsi_info[0].read_tunnel_complete) {
			ucsi_read_tunnel(0);
			message_in = pd_chip_ucsi_info[0].message_in;
			cci = &pd_chip_ucsi_info[0].cci;
		}

		if (pd_chip_ucsi_info[1].read_tunnel_complete) {
			ucsi_read_tunnel(1);
			message_in = pd_chip_ucsi_info[1].message_in;
			cci = &pd_chip_ucsi_info[1].cci;
		}


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

		if (
			*host_get_customer_memmap(EC_MEMMAP_UCSI_COMMAND) == UCSI_CMD_GET_CONNECTOR_STATUS &&
			(((uint8_t*)message_in)[8] & 0x03) > 1)
		{
			CPRINTS("Overriding Slow charger status");
			/* Override not charging value with nominal charging */
			((uint8_t*)message_in)[8] = (((uint8_t*)message_in)[8] & 0xFC) + 1;
		}

		msleep(2);

		memcpy(host_get_customer_memmap(EC_MEMMAP_UCSI_MESSAGE_IN), message_in, 16);
		memcpy(host_get_customer_memmap(EC_MEMMAP_UCSI_CCI), cci, 4);

		/**
		 * TODO: process the two pd results for one response.
		 */
		if (*host_get_customer_memmap(EC_MEMMAP_UCSI_COMMAND) == UCSI_CMD_GET_CAPABILITY)
			*host_get_customer_memmap(EC_MEMMAP_UCSI_MESSAGE_IN + 4) = 0x04;

		pd_chip_ucsi_info[0].read_tunnel_complete = 0;
		pd_chip_ucsi_info[1].read_tunnel_complete = 0;

		/* clear the UCSI command */
		if (!(*host_get_customer_memmap(0x00) & BIT(2)))
			*host_get_customer_memmap(EC_MEMMAP_UCSI_COMMAND) = 0;

		host_set_single_event(EC_HOST_EVENT_UCSI);
	}
}
