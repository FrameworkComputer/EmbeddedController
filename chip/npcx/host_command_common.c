/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fpsensor/fpsensor_detect.h"
#include "host_command.h"
#include "shi_chip.h"
#include "uart_host_command.h"

/* Store current transport type */
static enum fp_transport_type curr_transport_type = FP_TRANSPORT_TYPE_UNKNOWN;

/*
 * Get protocol information
 */
static enum ec_status
host_command_protocol_info(struct host_cmd_handler_args *args)
{
	enum ec_status ret_status = EC_RES_INVALID_COMMAND;

	/*
	 * Read transport type from TRANSPORT_SEL bootstrap pin the first
	 * time this function is called.
	 */
	if (IS_ENABLED(CONFIG_FINGERPRINT_MCU)) {
		if (curr_transport_type == FP_TRANSPORT_TYPE_UNKNOWN)
			curr_transport_type = get_fp_transport_type();

		if (IS_ENABLED(CONFIG_USART_HOST_COMMAND) &&
		    curr_transport_type == FP_TRANSPORT_TYPE_UART)
			ret_status = uart_get_protocol_info(args);
		else if (IS_ENABLED(CONFIG_HOST_INTERFACE_SHI) &&
			 curr_transport_type == FP_TRANSPORT_TYPE_SPI)
			ret_status = shi_get_protocol_info(args);
	} else if (IS_ENABLED(CONFIG_HOST_INTERFACE_SHI)) {
		ret_status = shi_get_protocol_info(args);
	}

	return ret_status;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, host_command_protocol_info,
		     EC_VER_MASK(0));
