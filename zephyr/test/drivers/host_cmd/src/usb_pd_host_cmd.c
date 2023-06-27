/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "usb_pd.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST_USER(usb_pd_host_cmd, test_hc_pd_host_event_status)
{
	struct ec_response_host_event_status response;
	struct host_cmd_handler_args args;

	/* Clear events */
	zassert_ok(ec_cmd_pd_host_event_status(&args, &response));

	/* Send arbitrary event */
	pd_send_host_event(1);

	zassert_ok(ec_cmd_pd_host_event_status(&args, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_true(response.status & 1);

	/* Send again to make sure the host command cleared the event */
	zassert_ok(ec_cmd_pd_host_event_status(&args, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.status, 0);
}

static struct ec_params_usb_pd_rw_hash_entry *
test_find_hc_remote_hash_entry(int dev_id)
{
	for (int i = 0; i < RW_HASH_ENTRIES; i++) {
		if (rw_hash_table[i].dev_id == dev_id) {
			return &rw_hash_table[i];
		}
	}

	return NULL;
}

ZTEST_USER(usb_pd_host_cmd, test_hc_remote_hash_entry__bad_dev_id)
{
	struct ec_params_usb_pd_rw_hash_entry params = {
		/* Dev ID can't be 0 */
		.dev_id = 0,
	};

	zassert_equal(ec_cmd_usb_pd_rw_hash_entry(NULL, &params),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(usb_pd_host_cmd, test_hc_remote_hash_entry__add_entry)
{
	struct ec_params_usb_pd_rw_hash_entry params = {
		/* Arbitrary dev_id */
		.dev_id = 1,
	};

	memset(rw_hash_table, 0,
	       RW_HASH_ENTRIES * sizeof(struct ec_params_usb_pd_rw_hash_entry));

	zassert_ok(ec_cmd_usb_pd_rw_hash_entry(NULL, &params));
	zassert_mem_equal(test_find_hc_remote_hash_entry(params.dev_id),
			  &params, sizeof(params));
}

ZTEST_USER(usb_pd_host_cmd, test_hc_remote_hash_entry__update_entry)
{
	int arbitrary_dev_id = 1;
	struct ec_params_usb_pd_rw_hash_entry initial_entry = {
		.dev_id = arbitrary_dev_id,
		/* Arbitrary reserved bytes */
		.reserved = 7,
	};
	struct ec_params_usb_pd_rw_hash_entry update_entry = {
		.dev_id = arbitrary_dev_id,
		/* Arbitrary different reserved bytes */
		.reserved = 3,
	};

	memset(rw_hash_table, 0,
	       RW_HASH_ENTRIES * sizeof(struct ec_params_usb_pd_rw_hash_entry));

	zassert_ok(ec_cmd_usb_pd_rw_hash_entry(NULL, &initial_entry));
	zassert_mem_equal(test_find_hc_remote_hash_entry(initial_entry.dev_id),
			  &initial_entry, sizeof(initial_entry));

	zassert_ok(ec_cmd_usb_pd_rw_hash_entry(NULL, &update_entry));
	zassert_mem_equal(test_find_hc_remote_hash_entry(update_entry.dev_id),
			  &update_entry, sizeof(update_entry));
}

ZTEST_USER(usb_pd_host_cmd, test_host_command_hc_pd_ports)
{
	struct ec_response_usb_pd_ports response;
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_usb_pd_ports(&args, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.num_ports, CONFIG_USB_PD_PORT_MAX_COUNT);
}

ZTEST_USER(usb_pd_host_cmd, test_typec_discovery_invalid_args)
{
	struct ec_params_typec_discovery params = {
		.port = 100,
		.partner_type = TYPEC_PARTNER_SOP,
	};
	struct ec_response_typec_discovery response;
	/* A successful EC_CMD_TYPEC_DISCOVERY requires response to be larger
	 * than ec_params_typec_discovery, but this one is expected to fail, so
	 * the response size is irrelevant.
	 */
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_TYPEC_DISCOVERY, 0, response, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	params.port = 0;
	/* This is not a valid enum value but should be representable. */
	params.partner_type = 5;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}

ZTEST_USER(usb_pd_host_cmd, test_typec_control_invalid_args)
{
	struct ec_params_typec_control params = {
		.port = 0,
		.command = TYPEC_CONTROL_COMMAND_TBT_UFP_REPLY,
	};

	/* Setting the TBT UFP responses is not supported by default. */
	zassert_equal(ec_cmd_typec_control(NULL, &params), EC_RES_UNAVAILABLE);

	/* Neither is mux setting. */
	params.command = TYPEC_CONTROL_COMMAND_USB_MUX_SET;
	zassert_equal(ec_cmd_typec_control(NULL, &params),
		      EC_RES_INVALID_PARAM);

	/* This is not a valid enum value but should be representable. */
	params.command = 0xff;
	zassert_equal(ec_cmd_typec_control(NULL, &params),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(usb_pd_host_cmd, test_typec_status_invalid_args)
{
	struct ec_params_typec_status params = {
		.port = 100,
	};
	struct ec_response_typec_status response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_TYPEC_STATUS, 0, response, params);

	/* An invalid port should result in an error. */
	zassert_equal(ec_cmd_typec_status(NULL, &params, &response),
		      EC_RES_INVALID_PARAM);

	params.port = 0;
	args.response_max = sizeof(struct ec_response_typec_status) - 1;
	zassert_equal(host_command_process(&args), EC_RES_RESPONSE_TOO_BIG);
}

ZTEST_SUITE(usb_pd_host_cmd, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
