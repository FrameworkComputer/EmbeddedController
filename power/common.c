/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common functionality across all chipsets */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SWITCH, format, ## args)

/*
 * Default timeout in us; if we've been waiting this long for an input
 * transition, just jump to the next state.
 */
#define DEFAULT_TIMEOUT SECOND

/* Timeout for dropping back from S5 to G3 */
#define S5_INACTIVITY_TIMEOUT (10 * SECOND)

static const char * const state_names[] = {
	"G3",
	"S5",
	"S3",
	"S0",
#ifdef CONFIG_POWER_S0IX
	"S0ix",
#endif
	"G3->S5",
	"S5->S3",
	"S3->S0",
	"S0->S3",
	"S3->S5",
	"S5->G3",
#ifdef CONFIG_POWER_S0IX
	"S0ix->S0",
	"S0->S0ix",
#endif
};

static uint32_t in_signals;   /* Current input signal states (IN_PGOOD_*) */
static uint32_t in_want;      /* Input signal state we're waiting for */
static uint32_t in_debug;     /* Signal values which print debug output */

static enum power_state state = POWER_G3;  /* Current state */
static int want_g3_exit;      /* Should we exit the G3 state? */
static uint64_t last_shutdown_time; /* When did we enter G3? */

#ifdef CONFIG_HIBERNATE
/* Delay before hibernating, in seconds */
static uint32_t hibernate_delay = CONFIG_HIBERNATE_DELAY_SEC;
#endif

#ifdef CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
/* Pause in S5 on shutdown? */
static int pause_in_s5;
#endif

static int power_signal_get_level(enum gpio_signal signal)
{
#ifdef CONFIG_POWER_S0IX
	return chipset_get_ps_debounced_level(signal);
#else
	return gpio_get_level(signal);
#endif
}

/**
 * Update input signals mask
 */
static void power_update_signals(void)
{
	uint32_t inew = 0;
	const struct power_signal_info *s = power_signal_list;
	int i;

	for (i = 0; i < POWER_SIGNAL_COUNT; i++, s++) {
		if (power_signal_get_level(s->gpio) == s->level)
			inew |= 1 << i;
	}

	if ((in_signals & in_debug) != (inew & in_debug))
		CPRINTS("power in 0x%04x", inew);

	in_signals = inew;
}

uint32_t power_get_signals(void)
{
	return in_signals;
}

int power_has_signals(uint32_t want)
{
	if ((in_signals & want) == want)
		return 1;

	CPRINTS("power lost input; wanted 0x%04x, got 0x%04x",
		want, in_signals & want);

	return 0;
}

int power_wait_signals(uint32_t want)
{
	in_want = want;
	if (!want)
		return EC_SUCCESS;

	while ((in_signals & in_want) != in_want) {
		if (task_wait_event(DEFAULT_TIMEOUT) == TASK_EVENT_TIMER) {
			power_update_signals();
			CPRINTS("power timeout on input; "
				"wanted 0x%04x, got 0x%04x",
				in_want, in_signals & in_want);
			return EC_ERROR_TIMEOUT;
		}
		/*
		 * TODO(crosbug.com/p/23772): should really shrink the
		 * remaining timeout if we woke up but didn't have all the
		 * signals we wanted.  Also need to handle aborts if we're no
		 * longer in the same state we were when we started waiting.
		 */
	}
	return EC_SUCCESS;
}

void power_set_state(enum power_state new_state)
{
	/* Record the time we go into G3 */
	if (new_state == POWER_G3)
		last_shutdown_time = get_time().val;

	state = new_state;

	/*
	 * Reset want_g3_exit flag here to prevent the situation that if the
	 * error handler in POWER_S5S3 decides to force shutdown the system and
	 * the flag is set, the system will go to G3 and then immediately exit
	 * G3 again.
	 */
	if (state == POWER_S5S3)
		want_g3_exit = 0;
}

/**
 * Common handler for steady states
 *
 * @param state		Current power state
 * @return Updated power state
 */
static enum power_state power_common_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		if (want_g3_exit) {
			want_g3_exit = 0;
			return POWER_G3S5;
		}

		in_want = 0;
#ifdef CONFIG_HIBERNATE
		if (extpower_is_present())
			task_wait_event(-1);
		else {
			uint64_t target_time;
			uint64_t time_now = get_time().val;
			uint32_t delay = hibernate_delay;
#ifdef CONFIG_HIBERNATE_BATT_PCT
			if (charge_get_percent() <= CONFIG_HIBERNATE_BATT_PCT
			    && CONFIG_HIBERNATE_BATT_SEC < delay)
				delay = CONFIG_HIBERNATE_BATT_SEC;
#endif
			target_time = last_shutdown_time + delay * 1000000ull;
			if (time_now > target_time) {
				/*
				 * Time's up.  Hibernate until wake pin
				 * asserted.
				 */
#ifdef CONFIG_LOW_POWER_PSEUDO_G3
				enter_pseudo_g3();
#else
				CPRINTS("hibernating");
				system_hibernate(0, 0);
#endif
			} else {
				uint64_t wait = target_time - time_now;
				if (wait > TASK_MAX_WAIT_US)
					wait = TASK_MAX_WAIT_US;

				/* Wait for a message */
				task_wait_event(wait);
			}
		}
#else /* !CONFIG_HIBERNATE */
		task_wait_event(-1);
#endif
		break;

	case POWER_S5:
		/*
		 * If the power button is pressed before S5 inactivity timer
		 * expires, the timer will be cancelled and the task of the
		 * power state machine will be back here again. Since we are
		 * here, which means the system has been waiting for CPU
		 * starting up, we don't need want_g3_exit flag to be set
		 * anymore. Therefore, we can reset the flag here to prevent
		 * the situation that the flag is still set after S5 inactivity
		 * timer expires, which can cause the system to exit G3 again.
		 */
		want_g3_exit = 0;

		/* Wait for inactivity timeout */
		power_wait_signals(0);
		if (task_wait_event(S5_INACTIVITY_TIMEOUT) ==
		    TASK_EVENT_TIMER) {
			/* Prepare to drop to G3; wake not requested yet */
			return POWER_S5G3;
		}
		break;

	case POWER_S3:
		/* Wait for a message */
		power_wait_signals(0);
		task_wait_event(-1);
		break;

	case POWER_S0:
		/* Wait for a message */
		power_wait_signals(0);
		task_wait_event(-1);
		break;
#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		/* Wait for a message */
		power_wait_signals(0);
		task_wait_event(-1);
		break;
#endif
	default:
		/* No common functionality for transition states */
		break;
	}

	return state;
}

/*****************************************************************************/
/* Chipset interface */

int chipset_in_state(int state_mask)
{
	int need_mask = 0;

	/*
	 * TODO(crosbug.com/p/23773): what to do about state transitions?  If
	 * the caller wants HARD_OFF|SOFT_OFF and we're in G3S5, we could still
	 * return non-zero.
	 */
	switch (state) {
	case POWER_G3:
		need_mask = CHIPSET_STATE_HARD_OFF;
		break;
	case POWER_G3S5:
	case POWER_S5G3:
		/*
		 * In between hard and soft off states.  Match only if caller
		 * will accept both.
		 */
		need_mask = CHIPSET_STATE_HARD_OFF | CHIPSET_STATE_SOFT_OFF;
		break;
	case POWER_S5:
		need_mask = CHIPSET_STATE_SOFT_OFF;
		break;
	case POWER_S5S3:
	case POWER_S3S5:
		need_mask = CHIPSET_STATE_SOFT_OFF | CHIPSET_STATE_SUSPEND;
		break;
	case POWER_S3:
		need_mask = CHIPSET_STATE_SUSPEND;
		break;
	case POWER_S3S0:
	case POWER_S0S3:
		need_mask = CHIPSET_STATE_SUSPEND | CHIPSET_STATE_ON;
		break;
	case POWER_S0:
		need_mask = CHIPSET_STATE_ON;
		break;
#ifdef CONFIG_POWER_S0IX
	case POWER_S0ixS0:
	case POWER_S0S0ix:
		need_mask = CHIPSET_STATE_ON | CHIPSET_STATE_STANDBY;
		break;
	case POWER_S0ix:
		need_mask = CHIPSET_STATE_STANDBY;
		break;
#endif
	}

	/* Return non-zero if all needed bits are present */
	return (state_mask & need_mask) == need_mask;
}

void chipset_exit_hard_off(void)
{
	/*
	 * If not in the soft-off state, hard-off state, or headed there,
	 * nothing to do.
	 */
	if (state != POWER_G3 && state != POWER_S5G3 && state != POWER_S5)
		return;

	/*
	 * Set a flag to leave G3, then wake the task. If the power state is
	 * POWER_S5G3, or is POWER_S5 but the S5 inactivity timer has
	 * expired, set this flag can let system go to G3 and then exit G3
	 * immediately for powering on.
	 */
	want_g3_exit = 1;

	/*
	 * If the power state is in POWER_S5 and S5 inactivity timer is
	 * running, to wake the chipset task can cancel S5 inactivity timer and
	 * then restart the timer. This will give cpu a chance to start up if
	 * S5 inactivity timer is about to expire while power button is
	 * pressed. For other states here, to wake the chipset task to trigger
	 * the event for leaving G3 is necessary.
	 */
	if (task_start_called())
		task_wake(TASK_ID_CHIPSET);
}

/*****************************************************************************/
/* Task function */

void chipset_task(void)
{
	enum power_state new_state;

	while (1) {
		CPRINTS("power state %d = %s, in 0x%04x",
			state, state_names[state], in_signals);

		/* Always let the specific chipset handle the state first */
		new_state = power_handle_state(state);

		/*
		 * If the state hasn't changed, run common steady-state
		 * handler.
		 */
		if (new_state == state)
			new_state = power_common_state(state);

		/* Handle state changes */
		if (new_state != state)
			power_set_state(new_state);
	}
}

/*****************************************************************************/
/* Hooks */

static void power_common_init(void)
{
	const struct power_signal_info *s = power_signal_list;
	int i;

	/* Update input state */
	power_update_signals();

	/* Call chipset-specific init to set initial state */
	power_set_state(power_chipset_init());

	/* Enable interrupts for input signals */
	for (i = 0; i < POWER_SIGNAL_COUNT; i++, s++)
		gpio_enable_interrupt(s->gpio);

	/*
	 * Update input state again since there is a small window
	 * before GPIO is enabled.
	 */
	power_update_signals();
}
DECLARE_HOOK(HOOK_INIT, power_common_init, HOOK_PRIO_INIT_CHIPSET);

static void power_lid_change(void)
{
	/* Wake up the task to update power state */
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_LID_CHANGE, power_lid_change, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_EXTPOWER
static void power_ac_change(void)
{
	if (extpower_is_present()) {
		CPRINTS("AC on");
	} else {
		CPRINTS("AC off");

		if (state == POWER_G3) {
			last_shutdown_time = get_time().val;
			task_wake(TASK_ID_CHIPSET);
		}
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, power_ac_change, HOOK_PRIO_DEFAULT);
#endif

/*****************************************************************************/
/* Interrupts */

#ifdef CONFIG_BRINGUP
#define MAX_SIGLOG_ENTRIES 24

static unsigned int siglog_entries;
static unsigned int siglog_truncated;

static struct {
	timestamp_t time;
	enum gpio_signal signal;
	int level;
} siglog[MAX_SIGLOG_ENTRIES];

static void siglog_deferred(void)
{
	unsigned int i;
	timestamp_t tdiff = {.val = 0};

	/* Disable interrupts for input signals while we print stuff.*/
	for (i = 0; i < POWER_SIGNAL_COUNT; i++)
		gpio_disable_interrupt(power_signal_list[i].gpio);

	CPRINTF("%d signal changes:\n", siglog_entries);
	for (i = 0; i < siglog_entries; i++) {
		if (i)
			tdiff.val = siglog[i].time.val - siglog[i-1].time.val;
		CPRINTF("  %.6ld  +%.6ld  %s => %d\n",
			siglog[i].time.val, tdiff.val,
			gpio_get_name(siglog[i].signal),
			siglog[i].level);
	}
	if (siglog_truncated)
		CPRINTF("  SIGNAL LOG TRUNCATED...\n");
	siglog_entries = siglog_truncated = 0;

	/* Okay, turn 'em on again. */
	for (i = 0; i < POWER_SIGNAL_COUNT; i++)
		gpio_enable_interrupt(power_signal_list[i].gpio);
}
DECLARE_DEFERRED(siglog_deferred);

static void siglog_add(enum gpio_signal signal)
{
	if (siglog_entries >= MAX_SIGLOG_ENTRIES) {
		siglog_truncated = 1;
		return;
	}

	siglog[siglog_entries].time = get_time();
	siglog[siglog_entries].signal = signal;
	siglog[siglog_entries].level = gpio_get_level(signal);
	siglog_entries++;

	hook_call_deferred(siglog_deferred, SECOND);
}

#define SIGLOG(S) siglog_add(S)

#else
#define SIGLOG(S)
#endif	/* CONFIG_BRINGUP */

#ifdef CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD
/*
 * Print an interrupt storm warning when we receive more than
 * CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD interrupts of a
 * single source within 1 second.
 */
static int power_signal_interrupt_count[POWER_SIGNAL_COUNT];

static void reset_power_signal_interrupt_count(void)
{
	int i;

	for (i = 0; i < POWER_SIGNAL_COUNT; ++i)
		power_signal_interrupt_count[i] = 0;
}
DECLARE_HOOK(HOOK_SECOND,
	     reset_power_signal_interrupt_count,
	     HOOK_PRIO_DEFAULT);
#endif

void power_signal_interrupt(enum gpio_signal signal)
{
#ifdef CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD
	int i;

	/* Tally our interrupts and print a warning if necessary. */
	for (i = 0; i < POWER_SIGNAL_COUNT; ++i) {
		if (power_signal_list[i].gpio == signal) {
			if (power_signal_interrupt_count[i]++ ==
			   CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD)
				CPRINTS("Interrupt storm! Signal %d\n", i);
			break;
		}
	}
#endif

	SIGLOG(signal);

	/* Shadow signals and compare with our desired signal state. */
	power_update_signals();

	/* Wake up the task */
	task_wake(TASK_ID_CHIPSET);
}

#ifdef CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
inline int power_get_pause_in_s5(void)
{
	return pause_in_s5;
}

inline void power_set_pause_in_s5(int pause)
{
	pause_in_s5 = pause;
}
#endif

/*****************************************************************************/
/* Console commands */

static int command_powerinfo(int argc, char **argv)
{
	/*
	 * Print power state in same format as state machine.  This is
	 * used by FAFT tests, so must match exactly.
	 */
	ccprints("power state %d = %s, in 0x%04x",
		 state, state_names[state], in_signals);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerinfo, command_powerinfo,
			NULL,
			"Show current power state",
			NULL);

#ifdef CONFIG_CMD_POWERINDEBUG
static int command_powerindebug(int argc, char **argv)
{
	const struct power_signal_info *s = power_signal_list;
	int i;
	char *e;

	/* If one arg, set the mask */
	if (argc == 2) {
		int m = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		in_debug = m;
	}

	/* Print the mask */
	ccprintf("power in:   0x%04x\n", in_signals);
	ccprintf("debug mask: 0x%04x\n", in_debug);

	/* Print the decode */

	ccprintf("bit meanings:\n");
	for (i = 0; i < POWER_SIGNAL_COUNT; i++, s++) {
		int mask = 1 << i;
		ccprintf("  0x%04x %d %s\n",
			 mask, in_signals & mask ? 1 : 0, s->name);
	}

	return EC_SUCCESS;
};
DECLARE_CONSOLE_COMMAND(powerindebug, command_powerindebug,
			"[mask]",
			"Get/set power input debug mask",
			NULL);
#endif

#ifdef CONFIG_HIBERNATE
static int command_hibernation_delay(int argc, char **argv)
{
	char *e;
	uint32_t time_g3 = ((uint32_t)(get_time().val - last_shutdown_time))
				/ SECOND;

	if (argc >= 2) {
		uint32_t s = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		hibernate_delay = s;
	}

	/* Print the current setting */
	ccprintf("Hibernation delay: %d s\n", hibernate_delay);
	if (state == POWER_G3 && !extpower_is_present()) {
		ccprintf("Time G3: %d s\n", time_g3);
		ccprintf("Time left: %d s\n", hibernate_delay - time_g3);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hibdelay, command_hibernation_delay,
			"[sec]",
			"Set the delay before going into hibernation",
			NULL);

static int host_command_hibernation_delay(struct host_cmd_handler_args *args)
{
	const struct ec_params_hibernation_delay *p = args->params;
	struct ec_response_hibernation_delay *r = args->response;

	uint32_t time_g3 = (uint32_t)((get_time().val - last_shutdown_time)
				      / SECOND);

	/* Only change the hibernation delay if seconds is non-zero. */
	if (p->seconds)
		hibernate_delay = p->seconds;

	if (state == POWER_G3 && !extpower_is_present())
		r->time_g3 = time_g3;
	else
		r->time_g3 = 0;

	if ((time_g3 != 0) && (time_g3 > hibernate_delay))
		r->time_remaining = 0;
	else
		r->time_remaining = hibernate_delay - time_g3;
	r->hibernate_delay = hibernate_delay;

	args->response_size = sizeof(struct ec_response_hibernation_delay);
	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HIBERNATION_DELAY,
		     host_command_hibernation_delay,
		     EC_VER_MASK(0));
#endif /* CONFIG_HIBERNATE */

#ifdef CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
static int host_command_pause_in_s5(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_set_value *p = args->params;
	struct ec_response_get_set_value *r = args->response;

	if (p->flags & EC_GSV_SET)
		pause_in_s5 = p->value;

	r->value = pause_in_s5;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GSV_PAUSE_IN_S5,
		     host_command_pause_in_s5,
		     EC_VER_MASK(0));

static int command_pause_in_s5(int argc, char **argv)
{
	if (argc > 1 && !parse_bool(argv[1], &pause_in_s5))
		return EC_ERROR_INVAL;

	ccprintf("pause_in_s5 = %s\n", pause_in_s5 ? "on" : "off");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pause_in_s5, command_pause_in_s5,
			"[on|off]",
			"Should the AP pause in S5 during shutdown?",
			NULL);
#endif /* CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5 */
