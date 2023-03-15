/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

/* Track last reported sleep event */
static enum host_sleep_event host_sleep_state;

/* LCOV_EXCL_START */
/* Function stub that has no behavior, so ignoring for coverage */
__overridable void
power_chipset_handle_host_sleep_event(enum host_sleep_event state,
				      struct host_sleep_event_context *ctx)
{
	/* Default weak implementation -- no action required. */
}
/* LCOV_EXCL_STOP */

static enum ec_status
host_command_host_sleep_event(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_sleep_event_v1 *p = args->params;
	struct ec_response_host_sleep_event_v1 *r = args->response;
	struct host_sleep_event_context ctx;
	enum host_sleep_event state = p->sleep_event;

	/*
	 * Treat a reboot after suspend as a resume for notification purposes
	 * (see b/273327518 for more details)
	 */
	if (host_sleep_state == HOST_SLEEP_EVENT_S0IX_SUSPEND && state == 0)
		state = HOST_SLEEP_EVENT_S0IX_RESUME;

	host_sleep_state = state;
	ctx.sleep_transitions = 0;
	switch (state) {
	case HOST_SLEEP_EVENT_S0IX_SUSPEND:
	case HOST_SLEEP_EVENT_S3_SUSPEND:
	case HOST_SLEEP_EVENT_S3_WAKEABLE_SUSPEND:
		ctx.sleep_timeout_ms = EC_HOST_SLEEP_TIMEOUT_DEFAULT;

		/* The original version contained only state. */
		if (args->version >= 1)
			ctx.sleep_timeout_ms =
				p->suspend_params.sleep_timeout_ms;

		break;

	default:
		break;
	}

	power_chipset_handle_host_sleep_event(host_sleep_state, &ctx);
	switch (state) {
	case HOST_SLEEP_EVENT_S0IX_RESUME:
	case HOST_SLEEP_EVENT_S3_RESUME:
		if (args->version >= 1) {
			r->resume_response.sleep_transitions =
				ctx.sleep_transitions;

			args->response_size = sizeof(*r);
		}

		break;

	default:
		break;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, host_command_host_sleep_event,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

enum host_sleep_event power_get_host_sleep_state(void)
{
	return host_sleep_state;
}

void power_set_host_sleep_state(enum host_sleep_event state)
{
	host_sleep_state = state;
}

/* Flag to notify listeners about suspend/resume events. */
static enum sleep_notify_type sleep_notify = SLEEP_NOTIFY_NONE;

/*
 * Note: the following sleep_ functions do not get called in the S3 path on
 * Intel devices. On Intel devices, they are called in the S0ix path.
 */
void sleep_set_notify(enum sleep_notify_type notify)
{
	sleep_notify = notify;
}

void sleep_notify_transition(enum sleep_notify_type check_state, int hook_id)
{
	if (sleep_notify != check_state)
		return;

	hook_notify(hook_id);
	sleep_set_notify(SLEEP_NOTIFY_NONE);
}

#ifdef CONFIG_POWER_SLEEP_FAILURE_DETECTION

static uint16_t sleep_signal_timeout;
/* Non-const because it may be set by sleeptimeout console cmd */
static uint16_t host_sleep_timeout_default = CONFIG_SLEEP_TIMEOUT_MS;
static uint32_t sleep_signal_transitions;
static enum sleep_hang_type timeout_hang_type;

static void sleep_transition_timeout(void);
DECLARE_DEFERRED(sleep_transition_timeout);

/* LCOV_EXCL_START */
/* Function stub that has no behavior, so ignoring for coverage */
__overridable void power_board_handle_sleep_hang(enum sleep_hang_type hang_type)
{
	/* Default empty implementation */
}
/* LCOV_EXCL_STOP */

/* LCOV_EXCL_START */
/* Function stub that has no behavior, so ignoring for coverage */
__overridable void
power_chipset_handle_sleep_hang(enum sleep_hang_type hang_type)
{
	/* Default empty implementation */
}
/* LCOV_EXCL_STOP */

static void sleep_increment_transition(void)
{
	if ((sleep_signal_transitions & EC_HOST_RESUME_SLEEP_TRANSITIONS_MASK) <
	    EC_HOST_RESUME_SLEEP_TRANSITIONS_MASK)
		sleep_signal_transitions += 1;
}

void sleep_suspend_transition(void)
{
	sleep_increment_transition();
	hook_call_deferred(&sleep_transition_timeout_data, -1);
}

void sleep_resume_transition(void)
{
	sleep_increment_transition();

	/*
	 * Start the timer again to ensure the AP doesn't get itself stuck in
	 * a state where it's no longer in a sleep state (S0ix/S3), but from
	 * the Linux perspective is still suspended. Perhaps a bug in the SoC-
	 * internal periodic housekeeping code might result in a situation
	 * like this.
	 */
	if (sleep_signal_timeout) {
		timeout_hang_type = SLEEP_HANG_S0IX_RESUME;
		hook_call_deferred(&sleep_transition_timeout_data,
				   (uint32_t)sleep_signal_timeout * 1000);
	}
}

static void sleep_transition_timeout(void)
{
	/* Mark the timeout. */
	sleep_signal_transitions |= EC_HOST_RESUME_SLEEP_TIMEOUT;
	hook_call_deferred(&sleep_transition_timeout_data, -1);

	if (timeout_hang_type != SLEEP_HANG_NONE) {
		power_chipset_handle_sleep_hang(timeout_hang_type);
		power_board_handle_sleep_hang(timeout_hang_type);
	}
}

void sleep_start_suspend(struct host_sleep_event_context *ctx)
{
	uint16_t timeout = ctx->sleep_timeout_ms;

	sleep_signal_transitions = 0;

	/* Use default to indicate no timeout given. */
	if (timeout == EC_HOST_SLEEP_TIMEOUT_DEFAULT) {
		timeout = host_sleep_timeout_default;
	}

	/* Use 0xFFFF to disable the timeout */
	if (timeout == EC_HOST_SLEEP_TIMEOUT_INFINITE) {
		sleep_signal_timeout = 0;
		return;
	}

	sleep_signal_timeout = timeout;
	timeout_hang_type = SLEEP_HANG_S0IX_SUSPEND;
	hook_call_deferred(&sleep_transition_timeout_data,
			   (uint32_t)timeout * 1000);
}

void sleep_complete_resume(struct host_sleep_event_context *ctx)
{
	/*
	 * Ensure we don't schedule another sleep_transition_timeout
	 * if the the HOST_SLEEP_EVENT_S0IX_RESUME message arrives before
	 * the CHIPSET task transitions to the POWER_S0ixS0 state.
	 */
	sleep_signal_timeout = 0;
	hook_call_deferred(&sleep_transition_timeout_data, -1);
	ctx->sleep_transitions = sleep_signal_transitions;
}

void sleep_reset_tracking(void)
{
	sleep_signal_transitions = 0;
	sleep_signal_timeout = 0;
	timeout_hang_type = SLEEP_HANG_NONE;
}

static int command_sleep_fail_timeout(int argc, const char **argv)
{
	if (argc < 2) {
		/* no arguments - just print the current timeout */
	} else if (!strcasecmp(argv[1], "default")) {
		host_sleep_timeout_default = CONFIG_SLEEP_TIMEOUT_MS;
	} else if (!strcasecmp(argv[1], "infinite")) {
		host_sleep_timeout_default = EC_HOST_SLEEP_TIMEOUT_INFINITE;
	} else {
		char *e;
		int val;

		val = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;

		if (val <= 0 || val >= EC_HOST_SLEEP_TIMEOUT_INFINITE) {
			ccprintf("Error: timeout range is 1..%d [msec]\n",
				 EC_HOST_SLEEP_TIMEOUT_INFINITE - 1);
			return EC_ERROR_PARAM1;
		}

		host_sleep_timeout_default = val;
	}

	if (host_sleep_timeout_default == EC_HOST_SLEEP_TIMEOUT_INFINITE)
		ccprintf("Sleep failure detection timeout is disabled\n");
	else
		ccprintf("Sleep failure detection timeout is %d [msec]\n",
			 host_sleep_timeout_default);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sleeptimeout, command_sleep_fail_timeout,
			"[default | infinite | <msec>]",
			"Display or set host sleep failure detection timeout.\n"
			"Valid arguments are:\n"
			" default\n"
			" infinite - disables the timeout\n"
			" <msec> - custom length in milliseconds\n"
			" <none> - prints the current setting");

#else /* !CONFIG_POWER_SLEEP_FAILURE_DETECTION */

/* No action */
void sleep_suspend_transition(void)
{
}

void sleep_resume_transition(void)
{
}

void sleep_start_suspend(struct host_sleep_event_context *ctx)
{
}

void sleep_complete_resume(struct host_sleep_event_context *ctx)
{
}

void sleep_reset_tracking(void)
{
}

#endif /* CONFIG_POWER_SLEEP_FAILURE_DETECTION */
