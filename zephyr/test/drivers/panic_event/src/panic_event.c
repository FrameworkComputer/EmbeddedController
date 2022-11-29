/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for panic event.
 */

#include "common.h"
#include "ec_tasks.h"
#include "panic.h"
#include "system.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

struct host_events_ctx events_ctx;

static void before(void *unused)
{
	ARG_UNUSED(unused);
	host_events_save(&events_ctx);
	host_clear_events(0xffffffff);
}

static void after(void *unused)
{
	ARG_UNUSED(unused);
	host_events_restore(&events_ctx);
}

/**
 * @brief Test Suite: Verifies panic event functionality.
 */
ZTEST_SUITE(panic_event, NULL, NULL, before, after, NULL);

/**
 * @brief TestPurpose: Verify EC_HOST_EVENT_PANIC event is asserted on panic
 *
 * Expected Results
 *  - Success
 */
ZTEST_USER(panic_event, test_panic_event_notify)
{
#ifdef CONFIG_HOSTCMD_X86
	/* Enable the EC_HOST_EVENT_PANIC event in the lpc mask */
	host_event_t lpc_event_mask;
	host_event_t mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_PANIC);

	lpc_event_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, lpc_event_mask | mask);
#endif

	zassert_false(host_is_event_set(EC_HOST_EVENT_PANIC));
	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
	zassert_true(host_is_event_set(EC_HOST_EVENT_PANIC));
}
