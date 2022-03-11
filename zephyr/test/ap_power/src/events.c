/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for AP power events
 */

#include <device.h>

#include <logging/log.h>
#include <zephyr.h>
#include <ztest.h>

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"

#include "hooks.h"
#include "test_state.h"

/*
 * Structure passed to event listeners.
 */
struct events {
	struct ap_power_ev_callback cb;
	enum ap_power_events event;
	int count;
};

/*
 * Common handler.
 * Increment count, and store event received.
 */
static void ev_handler(struct ap_power_ev_callback *callback,
		       struct ap_power_ev_data data)
{
	struct events *ev = CONTAINER_OF(callback, struct events, cb);

	ev->count++;
	ev->event = data.event;
}

/**
 * @brief TestPurpose: Check registration
 *
 * @details
 * Validate that listeners can be registered, even multiple times
 *
 * Expected Results
 *  - Multiple registrations do not result in multiple calls.
 */
ZTEST(events, test_registration)
{
	static struct events cb;

	ap_power_ev_init_callback(&cb.cb, ev_handler, AP_POWER_RESET);
	ap_power_ev_add_callback(&cb.cb);
	ap_power_ev_send_callbacks(AP_POWER_RESET);
	zassert_equal(1, cb.count, "Callback not called");
	zassert_equal(AP_POWER_RESET, cb.event, "Wrong event");
	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(1, cb.count, "Callback called");

	ap_power_ev_remove_callback(&cb.cb);
	ap_power_ev_send_callbacks(AP_POWER_RESET);
	zassert_equal(1, cb.count, "Callback called");
	cb.count = 0;	/* Reset to make it clear */
	cb.event = 0;
	/* Add it twice */
	ap_power_ev_add_callback(&cb.cb);
	ap_power_ev_add_callback(&cb.cb);
	ap_power_ev_send_callbacks(AP_POWER_RESET);
	zassert_equal(1, cb.count, "Callback not called");
	zassert_equal(AP_POWER_RESET, cb.event, "Wrong event");
	ap_power_ev_remove_callback(&cb.cb);
	/* Second remove should be no-op */
	ap_power_ev_remove_callback(&cb.cb);
}

/**
 * @brief TestPurpose: Check event mask changes
 *
 * @details
 * Validate that listeners adjust the event mask.
 *
 * Expected Results
 *  - Event mask changes are honoured
 */
ZTEST(events, test_event_mask)
{
	static struct events cb;

	ap_power_ev_init_callback(&cb.cb, ev_handler, 0);
	ap_power_ev_add_callback(&cb.cb);
	ap_power_ev_send_callbacks(AP_POWER_RESET);
	zassert_equal(0, cb.count, "Callback called");
	ap_power_ev_init_callback(&cb.cb, ev_handler, AP_POWER_RESET);

	ap_power_ev_send_callbacks(AP_POWER_RESET);
	zassert_equal(1, cb.count, "Callback not called");
	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(1, cb.count, "Callback called");

	/* Add interest in event */
	cb.count = 0;
	ap_power_ev_add_events(&cb.cb, AP_POWER_SUSPEND);
	ap_power_ev_send_callbacks(AP_POWER_RESET);
	zassert_equal(1, cb.count, "Callback not called");
	zassert_equal(AP_POWER_RESET, cb.event, "Wrong event");
	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(2, cb.count, "Callback not called");
	zassert_equal(AP_POWER_SUSPEND, cb.event, "Wrong event");

	ap_power_ev_remove_callback(&cb.cb);
}

static int count_hook_shutdown, count_hook_startup;

static void hook_shutdown(void)
{
	count_hook_shutdown++;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, hook_shutdown, HOOK_PRIO_DEFAULT);

static void hook_startup(void)
{
	count_hook_startup++;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, hook_startup, HOOK_PRIO_DEFAULT);

/**
 * @brief TestPurpose: Verify correct interconnection with hook framework.
 *
 * @details
 * Validate that events get passed back to the hook subsystem.
 *
 * Expected Results
 *  - Events originating from the AP power event API get delivered via hooks.
 */
ZTEST(events, test_hooks)
{
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_equal(0, count_hook_shutdown, "shutdown hook called");
	zassert_equal(1, count_hook_startup, "startup hook not called");
	zassert_equal(0, count_hook_shutdown,
		"reset event, shutdown hook called");
	zassert_equal(1, count_hook_startup,
		"reset event, startup hook called");
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_equal(1, count_hook_shutdown, "shutdown hook not called");
	zassert_equal(1, count_hook_startup, "startup hook called");
}

/**
 * @brief Test Suite: Verifies AP power notification functionality.
 */
ZTEST_SUITE(events, ap_power_predicate_post_main,
	    NULL, NULL, NULL, NULL);
