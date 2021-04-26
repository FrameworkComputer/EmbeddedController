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

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

static struct pd_chip_ucsi_info_t pd_chip_ucsi_info[] = {
	[PD_CHIP_0] = {

	},
	[PD_CHIP_1] = {

	}
};

static int debug_enable = 0;

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
	if (debug_enable) {
		CPRINTS("UCSI Command: 0x%02x.", *command);
		CPRINTS("UCSI Control specific: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x.",
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC),
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC+1),
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC+2),
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC+3),
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC+4),
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC+5));
	}

	switch (*command) {
	case UCSI_CMD_CONNECTOR_RESET:
	case UCSI_CMD_GET_CONNECTOR_CAPABILITY:
	case UCSI_CMD_SET_UOM:
	case UCSI_CMD_SET_UOR:
	case UCSI_CMD_SET_PDR:
	case UCSI_CMD_GET_CAM_SUPPORTED:
	case UCSI_CMD_SET_NEW_CAM:
	case UCSI_CMD_GET_PDOS:
	case UCSI_CMD_GET_CABLE_PROPERTY:
	case UCSI_CMD_GET_CONNECTOR_STATUS:
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

	return rv;
}

int ucsi_read_tunnel(int controller)
{
	int rv;

	rv = cypd_read_reg_block(controller, CYP5525_CCI_REG,
		pd_chip_ucsi_info[controller].cci, 4);

	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_CCI_REG failed");

	/* we need to offset the pd connector number to correct number */
	if (controller == 1 && (pd_chip_ucsi_info[controller].cci[0] & 0x06))
		pd_chip_ucsi_info[controller].cci[0] += 0x04;

	rv = cypd_read_reg_block(controller, CYP5525_MESSAGE_IN_REG,
		pd_chip_ucsi_info[controller].message_in, 16);

	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_MESSAGE_IN_REG failed");

	pd_chip_ucsi_info[controller].read_tunnel_complete = 1;

	if (debug_enable) {
		CPRINTS("P%d CCI response: 0x%02x, 0x%02x, 0x%02x, 0x%02x.", controller,
		pd_chip_ucsi_info[controller].cci[0], pd_chip_ucsi_info[controller].cci[1],
		pd_chip_ucsi_info[controller].cci[2], pd_chip_ucsi_info[controller].cci[3]);
	}


	return EC_SUCCESS;
}

int cyp5525_ucsi_startup(int controller)
{
	int rv = EC_SUCCESS;
	int data;

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
			pd_chip_ucsi_info[controller].version, 2);

		if (rv != EC_SUCCESS)
			CPRINTS("UCSI start command fail!");

		memcpy(host_get_customer_memmap(EC_MEMMAP_UCSI_VERSION),
			pd_chip_ucsi_info[controller].version, 2);

		cypd_clear_int(controller, CYP5525_DEV_INTR);
	}
	return rv;
}

/**
 * Suggested by bios team, we don't use host command frequenctly.
 * So we need to polling the flags to get the ucsi event form host.
 */
static void check_ucsi_event_from_host(void);
DECLARE_DEFERRED(check_ucsi_event_from_host);

static void check_ucsi_event_from_host(void)
{
	uint8_t *message_in;
	uint8_t *cci;
	int read_complete = 0;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
			(*host_get_customer_memmap(0x00) & BIT(2))) {

		/**
		 * Following the specification, until the EC reads the VERSION register
		 * from CCGX's UCSI interface, it ignores all writes from the BIOS
		 */
		ucsi_write_tunnel();
		*host_get_customer_memmap(0x00) &= ~BIT(2);
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
			message_in = pd_chip_ucsi_info[0].message_in;
			cci = pd_chip_ucsi_info[0].cci;
		}

		if (pd_chip_ucsi_info[1].read_tunnel_complete) {
			message_in = pd_chip_ucsi_info[1].message_in;
			cci = pd_chip_ucsi_info[1].cci;
		}

		memcpy(host_get_customer_memmap(EC_MEMMAP_UCSI_MESSAGE_IN), message_in, 16);
		memcpy(host_get_customer_memmap(EC_MEMMAP_UCSI_CCI), cci, 4);

		/**
		 * TODO: process the two pd results for one response.
		 */
		if (*host_get_customer_memmap(EC_MEMMAP_UCSI_COMMAND) == UCSI_CMD_GET_CAPABILITY)
			*host_get_customer_memmap(EC_MEMMAP_UCSI_MESSAGE_IN + 4) = 0x04;

		pd_chip_ucsi_info[0].read_tunnel_complete = 0;
		pd_chip_ucsi_info[1].read_tunnel_complete = 0;

		host_set_single_event(EC_HOST_EVENT_UCSI);

        /* clear the UCSI command */
        *host_get_customer_memmap(EC_MEMMAP_UCSI_COMMAND) = 0;
	}

	hook_call_deferred(&check_ucsi_event_from_host_data, 10 * MSEC);

}
DECLARE_HOOK(HOOK_INIT, check_ucsi_event_from_host, HOOK_PRIO_DEFAULT);
