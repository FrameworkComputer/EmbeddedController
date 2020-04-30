/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fpsensor_detect.h"
#include "host_command.h"
#include "spi.h"
#include "usart_host_command.h"

#ifndef CONFIG_I2C_SLAVE

/* Store current transport type */
static enum fp_transport_type curr_transport_type = FP_TRANSPORT_TYPE_UNKNOWN;

/*
 * Get protocol information
 */
static enum ec_status host_command_protocol_info(struct host_cmd_handler_args
						 *args)
{
	enum ec_status ret_status = EC_RES_INVALID_COMMAND;

	/*
	 * If FP sensor task is enabled, read transport type from TRANSPORT_SEL
	 * bootstrap pin for the first time this function is called.
	 */
	if ((IS_ENABLED(HAS_TASK_FPSENSOR)) &&
	    (curr_transport_type == FP_TRANSPORT_TYPE_UNKNOWN)) {
		curr_transport_type = get_fp_transport_type();
	}

	/*
	 * Transport select is only enabled on boards with fp sensor tasks.
	 * If fp sensor task is enabled, transport is USART and
	 * host command layer is present, call usart_get_protocol.
	 * If fp sensor task is enabled and transport is SPI or else if only
	 * spi layer is enabled on non fp boards, call spi_get_protocol_info.
	 */
	if (IS_ENABLED(HAS_TASK_FPSENSOR) &&
	    IS_ENABLED(CONFIG_USART_HOST_COMMAND) &&
	    curr_transport_type == FP_TRANSPORT_TYPE_UART)
		ret_status = usart_get_protocol_info(args);
	else if (IS_ENABLED(CONFIG_SPI) ||
		(IS_ENABLED(HAS_TASK_FPSENSOR) &&
		 curr_transport_type == FP_TRANSPORT_TYPE_SPI))
			ret_status = spi_get_protocol_info(args);

	return ret_status;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		     host_command_protocol_info,
		     EC_VER_MASK(0));

#endif /* CONFIG_I2C_SLAVE */
