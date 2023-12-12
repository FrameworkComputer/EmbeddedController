/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#define ARBITRARY_SLEEP_TRANSITIONS 1

/*
 * TODO(b/253224061): Reorganize fakes by public interface
 */
/* Fake to allow full linking */
FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
FAKE_VALUE_FUNC(enum power_state, power_chipset_init);
const struct power_signal_info power_signal_list[] = {};

FAKE_VOID_FUNC(power_chipset_handle_host_sleep_event, enum host_sleep_event,
	       struct host_sleep_event_context *);
FAKE_VOID_FUNC(power_chipset_handle_sleep_hang, enum sleep_hang_type);
FAKE_VOID_FUNC(power_board_handle_sleep_hang, enum sleep_hang_type);

/* Per-Test storage of host_sleep_event_context to validate argument values */
static struct host_sleep_event_context test_saved_context;

/* Test-specific custom fake */
static void _test_power_chipset_handle_host_sleep_event(
	enum host_sleep_event state, struct host_sleep_event_context *ctx)
{
	switch (state) {
	case HOST_SLEEP_EVENT_S0IX_RESUME:
	case HOST_SLEEP_EVENT_S3_RESUME:
		ctx->sleep_transitions = ARBITRARY_SLEEP_TRANSITIONS;
		break;

	case HOST_SLEEP_EVENT_S3_SUSPEND:
	case HOST_SLEEP_EVENT_S0IX_SUSPEND:
	case HOST_SLEEP_EVENT_S3_WAKEABLE_SUSPEND:
		break;
	}

	memcpy(&test_saved_context, ctx,
	       sizeof(struct host_sleep_event_context));
}

static void power_host_sleep_before_after(void *test_data)
{
	ARG_UNUSED(test_data);

	RESET_FAKE(power_chipset_handle_host_sleep_event);
	RESET_FAKE(power_chipset_handle_sleep_hang);
	RESET_FAKE(power_board_handle_sleep_hang);
	memset(&test_saved_context, 0, sizeof(struct host_sleep_event_context));

	sleep_reset_tracking();
}

ZTEST_USER(power_host_sleep, test_non_existent_sleep_event_v1__bad_event)
{
	struct ec_params_host_sleep_event_v1 p = {
		/* No such sleep event */
		.sleep_event = UINT8_MAX,
		/* Non-existent sleep event, so suspend params don't matter */
		.suspend_params = { 0 },
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args;

	/* Clear garbage for verifiable value */
	r.resume_response.sleep_transitions = 0;

	power_chipset_handle_host_sleep_event_fake.custom_fake =
		_test_power_chipset_handle_host_sleep_event;

	zassert_ok(ec_cmd_host_sleep_event_v1(&args, &p, &r));
	zassert_equal(args.response_size, 0);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 1);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      p.sleep_event);

	/*
	 * Unknown host sleep events don't retrieve sleep transitions from
	 * chip-specific handler.
	 */
	zassert_equal(r.resume_response.sleep_transitions, 0);
}

ZTEST_USER(power_host_sleep, test_non_existent_sleep_event_v1__s3_suspend)
{
	struct ec_params_host_sleep_event_v1 p = {
		.sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND,
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args;

	/* Set m/lsb of uint16_t to check for type coercion errors */
	p.suspend_params.sleep_timeout_ms = BIT(15) + 1;

	power_chipset_handle_host_sleep_event_fake.custom_fake =
		_test_power_chipset_handle_host_sleep_event;

	zassert_ok(ec_cmd_host_sleep_event_v1(&args, &p, &r));
	zassert_equal(args.response_size, 0);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 1);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      p.sleep_event);

	/*
	 * Verify sleep timeout propagated to chip-specific handler to use.
	 */
	zassert_equal(test_saved_context.sleep_timeout_ms,
		      p.suspend_params.sleep_timeout_ms);
}

ZTEST_USER(power_host_sleep, test_non_existent_sleep_event_v1__s3_resume)
{
	struct ec_params_host_sleep_event_v1 p = {
		.sleep_event = HOST_SLEEP_EVENT_S3_RESUME,
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args;

	power_chipset_handle_host_sleep_event_fake.custom_fake =
		_test_power_chipset_handle_host_sleep_event;

	zassert_ok(ec_cmd_host_sleep_event_v1(&args, &p, &r));
	zassert_equal(args.response_size, sizeof(r));
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 1);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      p.sleep_event);

	/*
	 * Verify sleep context propagated from chip-specific handler.
	 */
	zassert_equal(r.resume_response.sleep_transitions,
		      ARBITRARY_SLEEP_TRANSITIONS);
}

ZTEST(power_host_sleep, test_sleep_start_suspend_custom_timeout)
{
	struct host_sleep_event_context context = {
		/* Arbitrary 5ms timeout */
		.sleep_timeout_ms = 5,
	};

	sleep_start_suspend(&context);
	/*
	 * Validate that function idempotent wrt calling chip-specific handlers
	 */
	sleep_start_suspend(&context);

	/* Verify handlers not called because timeout didn't occur yet */
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 0);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 0);

	/* Allow timeout to occur */
	k_sleep(K_MSEC(CONFIG_SLEEP_TIMEOUT_MS * 2));

#if defined(SECTION_IS_RW)
	/* Check timeout handlers fired only *once* after multiple calls */
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 1);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 1);

	zassert_equal(power_chipset_handle_sleep_hang_fake.arg0_val,
		      SLEEP_HANG_S0IX_SUSPEND);
	zassert_equal(power_board_handle_sleep_hang_fake.arg0_val,
		      SLEEP_HANG_S0IX_SUSPEND);
#endif /* SECTION_IS_RW */
}

ZTEST(power_host_sleep, test_sleep_start_suspend_default_timeout)
{
	struct host_sleep_event_context context = {
		.sleep_timeout_ms = EC_HOST_SLEEP_TIMEOUT_DEFAULT,
	};

	sleep_start_suspend(&context);

	k_msleep(CONFIG_SLEEP_TIMEOUT_MS * 2);

#if defined(SECTION_IS_RW)
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 1);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 1);

	zassert_equal(power_chipset_handle_sleep_hang_fake.arg0_val,
		      SLEEP_HANG_S0IX_SUSPEND);
	zassert_equal(power_board_handle_sleep_hang_fake.arg0_val,
		      SLEEP_HANG_S0IX_SUSPEND);
#endif /* SECTION_IS_RW */
}

ZTEST(power_host_sleep, test_sleep_start_suspend_infinite_timeout)
{
	struct host_sleep_event_context context = {
		.sleep_timeout_ms = EC_HOST_SLEEP_TIMEOUT_INFINITE,
	};

	sleep_start_suspend(&context);

	k_sleep(K_MSEC(CONFIG_SLEEP_TIMEOUT_MS * 2));

	/* Verify that default handlers were never called */
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 0);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 0);
}

ZTEST(power_host_sleep, test_suspend_then_resume_with_timeout)
{
	struct host_sleep_event_context context = {
		.sleep_timeout_ms = EC_HOST_SLEEP_TIMEOUT_DEFAULT,
		.sleep_transitions = 0.
	};

	/* Start then suspend process with deferred hook call */
	sleep_start_suspend(&context);
	/* Register the suspend transition (cancels timeout hook) */
	sleep_suspend_transition();
	k_sleep(K_MSEC(CONFIG_SLEEP_TIMEOUT_MS * 2));

	/* No timeout hooks should've fired */
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 0);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 0);

	/* Transition to resume state and wait for hang timeout */
	sleep_resume_transition();
	k_sleep(K_MSEC(CONFIG_SLEEP_TIMEOUT_MS * 2));

#if defined(SECTION_IS_RW)
	/* Resume state transition timeout hook should've fired */
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 1);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 1);
	zassert_equal(power_chipset_handle_sleep_hang_fake.arg0_val,
		      SLEEP_HANG_S0IX_RESUME);
	zassert_equal(power_board_handle_sleep_hang_fake.arg0_val,
		      SLEEP_HANG_S0IX_RESUME);

	/* Complete resume so we can inspect the state transitions */
	sleep_complete_resume(&context);

	/* Transitioned to suspend and then to resume state. */
	zassert_equal(context.sleep_transitions &
			      EC_HOST_RESUME_SLEEP_TRANSITIONS_MASK,
		      2);
	/* There was a timeout */
	zassert_true(context.sleep_transitions & EC_HOST_RESUME_SLEEP_TIMEOUT);
#endif /* SECTION_IS_RW */
}

ZTEST(power_host_sleep, test_suspend_then_resume_with_reboot)
{
	struct ec_params_host_sleep_event_v1 p;
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args;
	struct host_sleep_event_context context = {
		.sleep_timeout_ms = EC_HOST_SLEEP_TIMEOUT_DEFAULT,
		.sleep_transitions = 0.
	};

	/* Start then suspend process like the OS would */
	p.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND;
	zassert_ok(ec_cmd_host_sleep_event_v1(&args, &p, &r));

	/* Verify we notified the chipset */
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 1);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      p.sleep_event);

	/* Now kick the internals as if we suspend and then fail to resume */
	sleep_start_suspend(&context);
	/* Register the suspend transition (cancels timeout hook) */
	sleep_suspend_transition();
	k_sleep(K_MSEC(CONFIG_SLEEP_TIMEOUT_MS * 2));

	/* No timeout hooks should've fired */
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 0);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 0);

	/* Transition to resume state and wait for hang timeout */
	sleep_resume_transition();
	k_sleep(K_MSEC(CONFIG_SLEEP_TIMEOUT_MS * 2));

#if defined(SECTION_IS_RW)
	/* Resume state transition timeout hook should've fired */
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 1);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 1);
	zassert_equal(power_chipset_handle_sleep_hang_fake.arg0_val,
		      SLEEP_HANG_S0IX_RESUME);
	zassert_equal(power_board_handle_sleep_hang_fake.arg0_val,
		      SLEEP_HANG_S0IX_RESUME);

	/* But now the OS says it's actually rebooted */
	p.sleep_event = 0;
	zassert_ok(ec_cmd_host_sleep_event_v1(&args, &p, &r));

	/* Verify we alerted as if this was a resume */
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 2);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      HOST_SLEEP_EVENT_S0IX_RESUME);
#endif /* SECTION_IS_RW */
}

ZTEST(power_host_sleep, test_suspend_then_reboot)
{
	struct ec_params_host_sleep_event_v1 p;
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args;
	struct host_sleep_event_context context = {
		.sleep_timeout_ms = EC_HOST_SLEEP_TIMEOUT_DEFAULT,
		.sleep_transitions = 0.
	};

	/* Start then suspend process like the OS would */
	p.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND;
	zassert_ok(ec_cmd_host_sleep_event_v1(&args, &p, &r));

	/* Verify we notified the chipset */
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 1);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      p.sleep_event);

	/* Now kick the internals as if we suspend and then fail to resume */
	sleep_start_suspend(&context);
	/* Register the suspend transition (cancels timeout hook) */
	sleep_suspend_transition();
	k_sleep(K_MSEC(CONFIG_SLEEP_TIMEOUT_MS * 2));

	/* No timeout hooks should've fired */
	zassert_equal(power_chipset_handle_sleep_hang_fake.call_count, 0);
	zassert_equal(power_board_handle_sleep_hang_fake.call_count, 0);

	/* Transition to resume state and then send that we rebooted instead */
	sleep_resume_transition();
	p.sleep_event = 0;
	zassert_ok(ec_cmd_host_sleep_event_v1(&args, &p, &r));

	/* Verify we alerted as if this was a resume */
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 2);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      HOST_SLEEP_EVENT_S0IX_RESUME);
}

/* Only used in test_sleep_set_notify */
static bool _test_host_sleep_hook_called;

static void _test_sleep_notify_hook(void)
{
	_test_host_sleep_hook_called = true;
}
DECLARE_HOOK(HOOK_TEST_1, _test_sleep_notify_hook, HOOK_PRIO_DEFAULT);

ZTEST(power_host_sleep, test_sleep_set_notify)
{
	/* Init as none */
	sleep_set_notify(SLEEP_NOTIFY_NONE);

	/* Verify hook may be notified for a specific NOTIFY state */
	_test_host_sleep_hook_called = false;
	sleep_set_notify(SLEEP_NOTIFY_SUSPEND);
	sleep_notify_transition(SLEEP_NOTIFY_SUSPEND, HOOK_TEST_1);
	k_sleep(K_SECONDS(1));

	zassert_true(_test_host_sleep_hook_called);

	/* Verify NOTIFY state is reset after firing hook */
	_test_host_sleep_hook_called = false;
	sleep_notify_transition(SLEEP_NOTIFY_SUSPEND, HOOK_TEST_1);
	k_sleep(K_SECONDS(1));

	zassert_false(_test_host_sleep_hook_called);

	/*
	 * Verify that SLEEP_NOTIFY_NONE is a potential hook state to fire
	 * TODO(b/253480505) Should this really be allowed?
	 */
	_test_host_sleep_hook_called = false;
	sleep_notify_transition(SLEEP_NOTIFY_NONE, HOOK_TEST_1);
	k_sleep(K_SECONDS(1));

	zassert_true(_test_host_sleep_hook_called);
}

ZTEST(power_host_sleep, test_set_get_host_sleep_state)
{
	power_set_host_sleep_state(HOST_SLEEP_EVENT_S3_RESUME);
	zassert_equal(power_get_host_sleep_state(), HOST_SLEEP_EVENT_S3_RESUME);

	power_set_host_sleep_state(HOST_SLEEP_EVENT_S0IX_RESUME);
	zassert_equal(power_get_host_sleep_state(),
		      HOST_SLEEP_EVENT_S0IX_RESUME);
}

ZTEST(power_host_sleep, test_verify_increment_change_state)
{
	struct ec_params_s0ix_cnt params = { .flags = EC_S0IX_COUNTER_RESET };
	struct ec_response_s0ix_cnt rsp;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GET_S0IX_COUNTER, 0, rsp, params);

	/* Verify that counter is set to 0 */
	zassert_ok(host_command_process(&args), "Failed to get sleep counter");
	zassert_equal(rsp.s0ix_counter, 0);

	/* Simulate S0ix state */
	sleep_set_notify(SLEEP_NOTIFY_SUSPEND);
	sleep_notify_transition(SLEEP_NOTIFY_SUSPEND, HOOK_CHIPSET_SUSPEND);

	/* Confirm counter incrementation */
	params.flags = 0;
	zassert_ok(host_command_process(&args), "Failed to get sleep counter");
	zassert_equal(rsp.s0ix_counter, 1);

	/* Reset counter and re-fetch it to verify that reset works */
	params.flags = EC_S0IX_COUNTER_RESET;
	zassert_ok(host_command_process(&args), "Failed to get sleep counter");
	params.flags = 0;
	zassert_ok(host_command_process(&args), "Failed to get sleep counter");
	zassert_equal(rsp.s0ix_counter, 0);
}

ZTEST_SUITE(power_host_sleep, drivers_predicate_post_main, NULL,
	    power_host_sleep_before_after, power_host_sleep_before_after, NULL);
