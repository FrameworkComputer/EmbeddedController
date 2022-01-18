/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "ec_commands.h"
#include "host_command.h"
#include "test_state.h"

ZTEST_USER(usb_pd_host_cmd, test_host_command_hc_pd_ports)
{
	struct ec_response_usb_pd_ports response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_USB_PD_PORTS, 0,
					    response);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.num_ports,
		      CONFIG_PLATFORM_EC_USB_PD_PORT_MAX_COUNT, NULL);
}

ZTEST_SUITE(usb_pd_host_cmd, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
