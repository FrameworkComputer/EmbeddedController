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

int ucsi_write_tunnel(void)
{
	uint8_t *message_out = host_get_customer_memmap(EC_MEMMAP_UCSI_MESSAGE_OUT);
	uint8_t *command = host_get_customer_memmap(EC_MEMMAP_UCSI_COMMAND);
	int i;
    int offset = 0;
	int rv = EC_SUCCESS;

	/**
	 * Note that CONTROL data has always to be written after MESSAGE_OUT data is written
	 * A write to CONTROL (in CCGX) triggers processing of that command.
	 * Hence MESSAGE_OUT must be available before CONTROL is written to CCGX.
	 */

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

        if (*command == UCSI_CMD_GET_ALTERNATE_MODES)
            offset = 1;

        /** 
         * those command will control pd port, so we need to check the command connector number.
         */
        if (*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC + offset) > 0x02) {
			*host_get_customer_memmap(EC_MEMMAP_UCSI_CONTROL_SPECIFIC + offset) >>= 1;
			i = 1;
		} else
			i = 0;
		pd_chip_ucsi_info[i ? 0 : 1].read_tunnel_complete = 1;
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

	rv = cypd_read_reg_block(controller, CYP5525_MESSAGE_IN_REG,
		pd_chip_ucsi_info[controller].message_in, 16);

	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_MESSAGE_IN_REG failed");

	pd_chip_ucsi_info[controller].wait_ack = 1;
	pd_chip_ucsi_info[controller].read_tunnel_complete = 1;

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

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
			(*host_get_customer_memmap(0x00) & BIT(2))) {

		/**
		 * Following the specification, until the EC reads the VERSION register
		 * from CCGX's UCSI interface, it ignores all writes from the BIOS
		 */
		ucsi_write_tunnel();
		*host_get_customer_memmap(0x00) &= ~BIT(2);
	}

	if (pd_chip_ucsi_info[0].read_tunnel_complete && pd_chip_ucsi_info[1].read_tunnel_complete) {

		if (pd_chip_ucsi_info[0].wait_ack) {
			message_in = pd_chip_ucsi_info[0].message_in;
			cci = pd_chip_ucsi_info[0].cci;
			pd_chip_ucsi_info[0].wait_ack = 0;
		}

		if (pd_chip_ucsi_info[1].wait_ack) {
			message_in = pd_chip_ucsi_info[1].message_in;
			cci = pd_chip_ucsi_info[1].cci;
			pd_chip_ucsi_info[1].wait_ack = 0;
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
	}

	hook_call_deferred(&check_ucsi_event_from_host_data, 10 * MSEC);

}
DECLARE_HOOK(HOOK_INIT, check_ucsi_event_from_host, HOOK_PRIO_DEFAULT);
