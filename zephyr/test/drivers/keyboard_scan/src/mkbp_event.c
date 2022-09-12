/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <emul/emul_kb_raw.h>

#include "console.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

ZTEST(mkbp_event, host_command_get_events__empty)
{
	/* Issue a host command to get the next event (from any source) */
	uint16_t ret;
	struct ec_response_get_next_event response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_GET_NEXT_EVENT, 0, response);

	ret = host_command_process(&args);
	zassert_equal(EC_RES_UNAVAILABLE, ret,
		      "Expected EC_RES_UNAVAILABLE but got %d", ret);
}

ZTEST(mkbp_event, host_command_get_events__get_event)
{
	/* Dispatch a fake keyboard event and ensure it gets returned by the
	 * host command.
	 */
	int ret;

	struct ec_response_get_next_event expected_event = {
		.event_type = EC_MKBP_EVENT_KEY_MATRIX,
		.data.key_matrix = {
			/* Arbitrary key matrix data (uint8_t[13]) */
			0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb,
			0xc, 0xd
		},
	};

	/* Add the above event to the MKBP keyboard FIFO and raise the event */

	ret = mkbp_fifo_add(expected_event.event_type,
			    &expected_event.data.key_matrix);
	activate_mkbp_with_events(BIT(expected_event.event_type));

	zassert_equal(EC_SUCCESS, ret, "Got %d when adding to FIFO", ret);

	/* Retrieve this event via host command */

	struct ec_response_get_next_event response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_GET_NEXT_EVENT, 0, response);

	ret = host_command_process(&args);
	zassert_equal(EC_RES_SUCCESS, ret, "Expected EC_RES_SUCCESS but got %d",
		      ret);

	/* Compare event data in response */
	zassert_equal(expected_event.event_type, response.event_type,
		      "Got event type 0x%02x", response.event_type);
	zassert_mem_equal(&expected_event.data.key_matrix,
			  &response.data.key_matrix,
			  sizeof(expected_event.data.key_matrix),
			  "Event data payload does not match.");
}

/* Set up a mock for mkbp_send_event(). This function is called by the MKBP
 * event sources to signal that a new event is available for servicing. Since we
 * are unit testing just event handling code, we do not want the various event
 * source tasks to raise unexpected events during testing and throw us off.
 * This mock will essentially cause mkbp_send_event() to become a no-op and
 * block the reset of the EC code from raising events and interfering. The test
 * code will bypass this by calling mkbp_event.c's internal
 * `activate_mkbp_with_events()` directly.
 */
FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

static void reset_events(void *data)
{
	/* Clear any keyboard scan events (type EC_MKBP_EVENT_KEY_MATRIX) */
	mkbp_clear_fifo();

	/* Clear pending events */
	mkbp_event_clear_all();

	/* Mock reset */
	RESET_FAKE(mkbp_send_event);
	mkbp_send_event_fake.return_val = 1;
}

ZTEST_SUITE(mkbp_event, drivers_predicate_post_main, NULL, reset_events,
	    reset_events, NULL);
