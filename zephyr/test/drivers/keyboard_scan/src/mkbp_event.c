/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "mkbp_fifo.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <emul/emul_kb_raw.h>

/**
 * @brief FFF fake that will be registered as a callback to monitor the EC->AP
 *        interrupt pin. Implements `gpio_callback_handler_t`.
 */
FAKE_VOID_FUNC(interrupt_gpio_monitor, const struct device *,
	       struct gpio_callback *, gpio_port_pins_t);

/**
 * @brief Fixture to hold state while the suite is running.
 */
struct event_fixture {
	/** Configuration for the interrupt pin change callback */
	struct gpio_callback callback_config;
};

static struct event_fixture fixture;

ZTEST(mkbp_event, test_host_command_get_events__empty)
{
	/* Issue a host command to get the next event (from any source) */
	uint16_t ret;
	struct ec_response_get_next_event response;

	ret = ec_cmd_get_next_event(NULL, &response);
	zassert_equal(EC_RES_UNAVAILABLE, ret,
		      "Expected EC_RES_UNAVAILABLE but got %d", ret);
}

ZTEST(mkbp_event, test_activate_with_events)
{
	const struct device *gpio_dev = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_ap_ec_int_l), gpios));
	const int gpio_pin = DT_GPIO_PIN(DT_NODELABEL(gpio_ap_ec_int_l), gpios);

	/* Put the chipset to sleep */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_BUTTON);
	k_sleep(K_SECONDS(15));

	/* Activate with no events, should not trigger an interrupt */
	activate_mkbp_with_events(0);

	/* Check that GPIO is still 1 */
	zassert_equal(1, gpio_emul_output_get(gpio_dev, gpio_pin));
}

ZTEST(mkbp_event, test_host_command_host_event_wake_mask)
{
	struct ec_response_mkbp_event_wake_mask response = { 0 };
	struct ec_params_mkbp_event_wake_mask params = { 0 };

	/* Set the wake mask to 0x12345678 */
	params.action = SET_WAKE_MASK;
	params.mask_type = EC_MKBP_HOST_EVENT_WAKE_MASK;
	params.new_wake_mask = 0x12345678;

	zassert_ok(ec_cmd_mkbp_wake_mask(NULL, &params, &response));

	/* Get the wake mask */
	params.action = GET_WAKE_MASK;

	zassert_ok(ec_cmd_mkbp_wake_mask(NULL, &params, &response));
	zassert_equal(0x12345678, response.wake_mask);
}

ZTEST(mkbp_event, test_host_command_event_wake_mask)
{
	struct ec_response_mkbp_event_wake_mask response = { 0 };
	struct ec_params_mkbp_event_wake_mask params = { 0 };

	/* Set the wake mask to 0x87654321 */
	params.action = SET_WAKE_MASK;
	params.mask_type = EC_MKBP_EVENT_WAKE_MASK;
	params.new_wake_mask = 0x87654321;

	zassert_ok(ec_cmd_mkbp_wake_mask(NULL, &params, &response));

	/* Get the wake mask */
	params.action = GET_WAKE_MASK;

	zassert_ok(ec_cmd_mkbp_wake_mask(NULL, &params, &response));
	zassert_equal(0x87654321, response.wake_mask);
}

ZTEST(mkbp_event, test_host_command_wake_mask__invalid_args)
{
	struct ec_response_mkbp_event_wake_mask response = { 0 };
	struct ec_params_mkbp_event_wake_mask params = {
		.action = -1,
		.mask_type = -1,
	};

	/* Check invalid action */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_mkbp_wake_mask(NULL, &params, &response));

	/* Check invalid mask type in getter */
	params.action = GET_WAKE_MASK;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_mkbp_wake_mask(NULL, &params, &response));

	/* Check invalid mask type in setter */
	params.action = SET_WAKE_MASK;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_mkbp_wake_mask(NULL, &params, &response));
}

ZTEST(mkbp_event, test_console_command_wake_mask_event)
{
	check_console_cmd("mkbpwakemask event 500",
			  "MKBP event wake mask: 0x000001f4", 0, __FILE__,
			  __LINE__);
	check_console_cmd("mkbpwakemask hostevent 7934",
			  "MKBP host event wake mask: 0x00001efe", 0, __FILE__,
			  __LINE__);
	check_console_cmd("mkbpwakemask event f", NULL, EC_ERROR_PARAM2,
			  __FILE__, __LINE__);
	check_console_cmd("mkbpwakemask event", NULL, EC_ERROR_PARAM_COUNT,
			  __FILE__, __LINE__);
}

ZTEST(mkbp_event, test_host_command_get_events__get_event)
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
			    (const uint8_t *)&expected_event.data.key_matrix);
	activate_mkbp_with_events(BIT(expected_event.event_type));

	zassert_equal(EC_SUCCESS, ret, "Got %d when adding to FIFO", ret);

	/* Retrieve this event via host command */

	struct ec_response_get_next_event response;

	ret = ec_cmd_get_next_event(NULL, &response);
	zassert_equal(EC_RES_SUCCESS, ret, "Expected EC_RES_SUCCESS but got %d",
		      ret);

	/* Compare event data in response */
	zassert_equal(expected_event.event_type, response.event_type,
		      "Got event type 0x%02x", response.event_type);
	zassert_mem_equal(&expected_event.data.key_matrix,
			  &response.data.key_matrix,
			  sizeof(expected_event.data.key_matrix),
			  "Event data payload does not match.");

	/* Check for two pin change events (initial assertion when the event
	 * was sent, and a de-assertion once we retrieved it through the host
	 * command)
	 */

	zassert_equal(2, interrupt_gpio_monitor_fake.call_count,
		      "Only %d pin events",
		      interrupt_gpio_monitor_fake.call_count);
}

ZTEST(mkbp_event, test_host_command_get_events__get_event_v2)
{
	/*
	 * Dispatch some fake events and ensure they get returned by the
	 * host command. Event types must be different.
	 */

	const struct ec_response_get_next_event_v1 expected_event = {
		.event_type = EC_MKBP_EVENT_KEY_MATRIX,
		.data.key_matrix = {
			/* Arbitrary key matrix data (uint8_t[13]) */
			0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb,
			0xc, 0xd
		},
	};
	const struct ec_response_get_next_event_v1 expected_event2 = {
		.event_type = EC_MKBP_EVENT_BUTTON,
		.data.buttons = BIT(EC_MKBP_VOL_UP) | BIT(EC_MKBP_VOL_DOWN),
	};
	int ret;

	/*
	 * Add the above events to the MKBP keyboard FIFO and raise the
	 * events
	 */

	ret = mkbp_fifo_add(expected_event.event_type,
			    expected_event.data.key_matrix);
	zassert_equal(EC_SUCCESS, ret, "Got %d when adding to FIFO", ret);

	ret = mkbp_fifo_add(expected_event2.event_type,
			    expected_event2.data.key_matrix);
	zassert_equal(EC_SUCCESS, ret, "Got %d when adding to FIFO", ret);

	activate_mkbp_with_events(BIT(expected_event.event_type));
	activate_mkbp_with_events(BIT(expected_event2.event_type));

	/* Retrieve these events via host commands */

	struct ec_response_get_next_event_v1 response;

	ret = ec_cmd_get_next_event_v2(NULL, &response);
	zassert_equal(EC_RES_SUCCESS, ret, "Expected EC_RES_SUCCESS but got %d",
		      ret);
	zassert_true((response.event_type & EC_MKBP_HAS_MORE_EVENTS) != 0,
		     "Expected EC_MKBP_HAS_MORE_EVENTS but got 0x%x",
		     response.event_type);

	ret = ec_cmd_get_next_event_v2(NULL, &response);
	zassert_equal(EC_RES_SUCCESS, ret, "Expected EC_RES_SUCCESS but got %d",
		      ret);
	zassert_true((response.event_type & EC_MKBP_HAS_MORE_EVENTS) == 0,
		     "Expected no EC_MKBP_HAS_MORE_EVENTS but got 0x%x",
		     response.event_type);
}

ZTEST(mkbp_event, test_no_ap_response)
{
	/* Cause an event but do not send any host commands. This should cause
	 * the EC to send the interrupt to the AP 3 times before giving up.
	 * Use the GPIO emulator to monitor for interrupts.
	 */

	int ret;

	struct ec_response_get_next_event expected_event = {
		.event_type = EC_MKBP_EVENT_KEY_MATRIX,
	};

	ret = mkbp_fifo_add(expected_event.event_type,
			    (uint8_t *)&expected_event.data.key_matrix);
	activate_mkbp_with_events(BIT(expected_event.event_type));
	zassert_equal(EC_SUCCESS, ret, "Got %d when adding to FIFO", ret);

	/* EC will attempt to signal the interrupt 3 times. Each attempt lasts
	 * 1 second, so sleep for 5 and then count the number of times the
	 * interrupt pin was asserted. (It does not get de-asserted)
	 */

	k_sleep(K_SECONDS(5));

	zassert_equal(3, interrupt_gpio_monitor_fake.call_count,
		      "Interrupt pin asserted only %d times.",
		      interrupt_gpio_monitor_fake.call_count);
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

static void *setup(void)
{
	/* Add a callback to the EC->AP interrupt pin so we can log interrupt
	 * attempts with an FFF fake.
	 */

	const struct gpio_dt_spec *interrupt_pin =
		GPIO_DT_FROM_NODELABEL(gpio_ap_ec_int_l);

	fixture.callback_config = (struct gpio_callback){
		.pin_mask = BIT(interrupt_pin->pin),
		.handler = interrupt_gpio_monitor,
	};

	zassert_ok(gpio_add_callback(interrupt_pin->port,
				     &fixture.callback_config),
		   "Could not configure GPIO callback.");

	return &fixture;
}

static void teardown(void *data)
{
	/* Remove the GPIO callback on the interrupt pin */

	struct event_fixture *f = (struct event_fixture *)data;
	const struct gpio_dt_spec *interrupt_pin =
		GPIO_DT_FROM_NODELABEL(gpio_ap_ec_int_l);

	gpio_remove_callback(interrupt_pin->port, &f->callback_config);
}

static void reset_events(void *data)
{
	/* Clear any keyboard scan events (type EC_MKBP_EVENT_KEY_MATRIX) */
	mkbp_clear_fifo();

	/* Clear pending events */
	mkbp_event_clear_all();

	/* Mock reset */
	RESET_FAKE(interrupt_gpio_monitor);
	RESET_FAKE(mkbp_send_event);
	mkbp_send_event_fake.return_val = 1;

	test_set_chipset_to_s0();
}

ZTEST_SUITE(mkbp_event, drivers_predicate_post_main, setup, reset_events,
	    reset_events, teardown);
