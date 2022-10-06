/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "usb_pd.h"

ZTEST_USER(usb_pd_host_cmd, test_hc_pd_host_event_status)
{
	struct ec_response_host_event_status response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_PD_HOST_EVENT_STATUS, 0, response);

	/* Clear events */
	zassert_ok(host_command_process(&args));

	/* Send arbitrary event */
	pd_send_host_event(1);

	zassert_ok(host_command_process(&args));
	zassert_equal(args.response_size, sizeof(response));
	zassert_true(response.status & 1);

	/* Send again to make sure the host command cleared the event */
	zassert_ok(host_command_process(&args));
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
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_USB_PD_RW_HASH_ENTRY, 0, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}

ZTEST_USER(usb_pd_host_cmd, test_hc_remote_hash_entry__add_entry)
{
	struct ec_params_usb_pd_rw_hash_entry params = {
		/* Arbitrary dev_id */
		.dev_id = 1,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_USB_PD_RW_HASH_ENTRY, 0, params);

	memset(rw_hash_table, 0,
	       RW_HASH_ENTRIES * sizeof(struct ec_params_usb_pd_rw_hash_entry));

	zassert_ok(host_command_process(&args));
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
	struct host_cmd_handler_args initial_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_USB_PD_RW_HASH_ENTRY, 0, initial_entry);
	struct host_cmd_handler_args update_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_USB_PD_RW_HASH_ENTRY, 0, update_entry);

	memset(rw_hash_table, 0,
	       RW_HASH_ENTRIES * sizeof(struct ec_params_usb_pd_rw_hash_entry));

	zassert_ok(host_command_process(&initial_args));
	zassert_mem_equal(test_find_hc_remote_hash_entry(initial_entry.dev_id),
			  &initial_entry, sizeof(initial_entry));

	zassert_ok(host_command_process(&update_args));
	zassert_mem_equal(test_find_hc_remote_hash_entry(update_entry.dev_id),
			  &update_entry, sizeof(update_entry));
}

ZTEST_USER(usb_pd_host_cmd, test_host_command_hc_pd_ports)
{
	struct ec_response_usb_pd_ports response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_USB_PD_PORTS, 0, response);

	zassert_ok(host_command_process(&args));
	zassert_ok(args.result);
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.num_ports, CONFIG_USB_PD_PORT_MAX_COUNT);
}

ZTEST_SUITE(usb_pd_host_cmd, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
