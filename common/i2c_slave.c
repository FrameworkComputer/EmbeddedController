/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C slave cross-platform code for Chrome EC */

#include "host_command.h"
#include "i2c.h"
#include "util.h"
#ifndef CONFIG_HOSTCMD_X86
enum ec_status i2c_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = BIT(3);
	r->max_request_packet_size = I2C_MAX_HOST_PACKET_SIZE;
	r->max_response_packet_size = I2C_MAX_HOST_PACKET_SIZE;
	r->flags = 0;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		     i2c_get_protocol_info,
		     EC_VER_MASK(0));
#endif
