/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "config.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "system.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

/* Track last reported sleep event */
static enum host_sleep_event host_sleep_state;

#define SYSRQ_WAIT_MSEC 50

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

#ifdef CONFIG_POWERSEQ_S0IX_COUNTER
atomic_t s0ix_counter;

static void handle_chipset_suspend(void)
{
	atomic_inc(&s0ix_counter);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, handle_chipset_suspend, HOOK_PRIO_LAST);
#endif /* CONFIG_POWERSEQ_S0IX_COUNTER */

/**
 * Sleep Hang Recovery Routines.
 *
 * Only runs in RW to de-risk an unrecoverable boot loop in RO.
 */
#if defined(SECTION_IS_RW) && defined(CONFIG_POWER_SLEEP_FAILURE_DETECTION)

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

/* These counters are reset whenever there's a successful resume */
static int soft_sleep_hang_count;
static int hard_sleep_hang_count;

/* Shutdown or reset on hard hang */
static int shutdown_on_hard_hang;

static void board_handle_hard_sleep_hang(void);
DECLARE_DEFERRED(board_handle_hard_sleep_hang);

/**
 * Hard hang detection timers are stopped on any suspend, resume, reset or
 * shutdown event.
 */
static void stop_hard_hang_timer(void)
{
	hook_call_deferred(&board_handle_hard_sleep_hang_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, stop_hard_hang_timer, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, stop_hard_hang_timer, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESET, stop_hard_hang_timer, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, stop_hard_hang_timer, HOOK_PRIO_DEFAULT);

/**
 * Reboot or shutdown when hard sleep hang detected.
 * This timer is stopped on suspend, resume, reset or shutdown events.
 */
static void board_handle_hard_sleep_hang(void)
{
	hard_sleep_hang_count += 1;
	/* Avoid race condition */
	stop_hard_hang_timer();

	if (shutdown_on_hard_hang) {
		ccprints("Very hard S0ix sleep hang detected!!! "
			 "Shutting down AP now!");
		chipset_force_shutdown(CHIPSET_SHUTDOWN_BOARD_CUSTOM);
	} else {
		ccprints("Hard S0ix sleep hang detected!! Resetting AP now!");
		/* If AP reset does not break hang, force a shutdown */
		shutdown_on_hard_hang = true;
		ccprints("AP will be shutdown in %dms if hang persists",
			 CONFIG_HARD_SLEEP_HANG_TIMEOUT);
		hook_call_deferred(&board_handle_hard_sleep_hang_data,
				   CONFIG_HARD_SLEEP_HANG_TIMEOUT * MSEC);
		chipset_reset(CHIPSET_RESET_HANG_REBOOT);
	}
}

void power_sleep_hang_recovery(enum sleep_hang_type hang_type)
{
	soft_sleep_hang_count += 1;

	/* Avoid race condition */
	stop_hard_hang_timer();

	if (hang_type == SLEEP_HANG_S0IX_SUSPEND)
		ccprints("S0ix suspend sleep hang detected!");
	else if (hang_type == SLEEP_HANG_S0IX_RESUME)
		ccprints("S0ix resume sleep hang detected!");

	ccprints("Consecutive sleep hang count: soft=%d hard=%d",
		 soft_sleep_hang_count, hard_sleep_hang_count);

	if (hard_sleep_hang_count == 0) {
		/* Try an AP reset first */
		shutdown_on_hard_hang = false;
		ccprints("AP will be force reset in %dms if hang persists",
			 CONFIG_HARD_SLEEP_HANG_TIMEOUT);
	} else {
		/* Avoid reboot loop that drains battery and just shutdown */
		shutdown_on_hard_hang = true;
		ccprints("Consecutive(%d) hard sleep hangs detected!",
			 hard_sleep_hang_count);
		ccprints("AP will be force shutdown in %dms if hang persists",
			 CONFIG_HARD_SLEEP_HANG_TIMEOUT);
	}

	/*
	 * Start a timer to manually reset the AP, in case it's just slow.
	 */
	hook_call_deferred(&board_handle_hard_sleep_hang_data,
			   CONFIG_HARD_SLEEP_HANG_TIMEOUT * MSEC);

	/*
	 * Always send a host event, in case the AP is stuck in FW.
	 * This will be ignored if the AP is in the OS.
	 */
	CPRINTS("Warning: Detected sleep hang! Waking host up!");
	host_set_single_event(EC_HOST_EVENT_HANG_DETECT);

	if (IS_ENABLED(CONFIG_EMULATED_SYSRQ)) {
		/*
		 * Send |SysRq| signal to generate a kernel panic. If the AP is
		 * in the OS, this will generate stack traces for all of the
		 * running CPUs and trigger a reboot. A single |SysRq| restarts
		 * chrome, while two trigger a kernel panic.
		 * Otherwise, if the AP is not in the kernel, this will do
		 * nothing, so the device will continue to be hung until the
		 * timer expires and sysrq_reboot_timeout() is called to
		 * reboot the AP.
		 */
		CPRINTS("Sending SysRq to trigger AP kernel panic and reboot!");
		host_send_sysrq('x');
		/*
		 * Wait a bit so the AP can treat them as separate SysRq
		 * signals.
		 */
		usleep(SYSRQ_WAIT_MSEC * MSEC);
		host_send_sysrq('x');
	}
}

/**
 * Reset hang counters whenever a resume is successful
 */
static void reset_hang_counters(void)
{
	if (hard_sleep_hang_count || soft_sleep_hang_count)
		ccprints("Successful S0ix resume after consecutive hangs: "
			 "soft=%d hard=%d",
			 soft_sleep_hang_count, hard_sleep_hang_count);
	hard_sleep_hang_count = 0;
	soft_sleep_hang_count = 0;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, reset_hang_counters, HOOK_PRIO_DEFAULT);

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

	/*
	 * Perform the recovery after the chipset/board has had a chance to do
	 * their work, so we don't modify system state (resetting the AP) until
	 * after they've initiated any debug data collection.
	 */
	power_sleep_hang_recovery(timeout_hang_type);
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

#else /* !SECTION_IS_RW && !CONFIG_POWER_SLEEP_FAILURE_DETECTION */

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

#endif /* SECTION_IS_RW && CONFIG_POWER_SLEEP_FAILURE_DETECTION */
