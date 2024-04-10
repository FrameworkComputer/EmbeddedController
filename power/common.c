/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common functionality across all chipsets */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "display_7seg.h"
#include "espi.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"
#include "power.h"
#include "power/amd_x86.h"
#include "power/intel_x86.h"
#include "power/qcom.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

/*
 * Default timeout in us; if we've been waiting this long for an input
 * transition, just jump to the next state.
 */
#define DEFAULT_TIMEOUT SECOND

/* Timeout for dropping back from S5 to G3 in seconds */
#ifdef CONFIG_CMD_S5_TIMEOUT
static int s5_inactivity_timeout = 10;
#else
static const int s5_inactivity_timeout = 10;
#endif

static const char *const state_names[] = {
	"G3",	    "S5",	"S4",	  "S3",	    "S0",
#ifdef CONFIG_POWER_S0IX
	"S0ix",
#endif
	"G3->S5",   "S5->S3",	"S3->S0", "S0->S3", "S3->S5",
	"S5->G3",   "S3->S4",	"S4->S3", "S4->S5", "S5->S4",
#ifdef CONFIG_POWER_S0IX
	"S0ix->S0", "S0->S0ix",
#endif
};

static uint32_t in_signals; /* Current input signal states (IN_PGOOD_*) */
static uint32_t in_want; /* Input signal state we're waiting for */
static uint32_t in_debug; /* Signal values which print debug output */

static enum power_state state = POWER_G3; /* Current state */
static int want_g3_exit; /* Should we exit the G3 state? */
static uint64_t last_shutdown_time; /* When did we enter G3? */

#ifdef CONFIG_HIBERNATE
/* Delay before hibernating, in seconds */
static uint32_t hibernate_delay = CONFIG_HIBERNATE_DELAY_SEC;
#endif

#ifdef CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
/* Pause in S5 on shutdown? */
static int pause_in_s5;
#endif

static bool want_reboot_ap_at_g3; /* Want to reboot AP from G3? */
/* Want to reboot AP from G3 with delay? */
static uint64_t reboot_ap_at_g3_delay;

static enum ec_status
host_command_reboot_ap_on_g3(struct host_cmd_handler_args *args)
{
	const struct ec_params_reboot_ap_on_g3_v1 *cmd = args->params;

	/* Store request for processing at g3 */
	want_reboot_ap_at_g3 = true;

	switch (args->version) {
	case 0:
		break;
	case 1:
		/* Store user specified delay to wait in G3 state */
		reboot_ap_at_g3_delay = cmd->reboot_ap_at_g3_delay;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_REBOOT_AP_ON_G3, host_command_reboot_ap_on_g3,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

__overridable int power_signal_get_level(enum gpio_signal signal)
{
	if (IS_ENABLED(CONFIG_HOST_ESPI_VW_POWER_SIGNAL)) {
		/* Check signal is from GPIOs or VWs */
		if (espi_signal_is_vw(signal))
			return espi_vw_get_wire((enum espi_vw_signal)signal);
	}
	return gpio_get_level(signal);
}

int power_signal_disable_interrupt(enum gpio_signal signal)
{
	if (IS_ENABLED(CONFIG_HOST_ESPI_VW_POWER_SIGNAL)) {
		/* Check signal is from GPIOs or VWs */
		if (espi_signal_is_vw(signal))
			return espi_vw_disable_wire_int(
				(enum espi_vw_signal)signal);
	}
	return gpio_disable_interrupt(signal);
}

int power_signal_enable_interrupt(enum gpio_signal signal)
{
	if (IS_ENABLED(CONFIG_HOST_ESPI_VW_POWER_SIGNAL)) {
		/* Check signal is from GPIOs or VWs */
		if (espi_signal_is_vw(signal))
			return espi_vw_enable_wire_int(
				(enum espi_vw_signal)signal);
	}
	return gpio_enable_interrupt(signal);
}

int power_signal_is_asserted(const struct power_signal_info *s)
{
	return power_signal_get_level(s->gpio) ==
	       !!(s->flags & POWER_SIGNAL_ACTIVE_STATE);
}

#ifdef CONFIG_BRINGUP
static const char *power_signal_get_name(enum gpio_signal signal)
{
	if (IS_ENABLED(CONFIG_HOST_INTERFACE_ESPI)) {
		/* Check signal is from GPIOs or VWs */
		if (espi_signal_is_vw(signal))
			return espi_vw_get_wire_name(
				(enum espi_vw_signal)signal);
	}
	return gpio_get_name(signal);
}
#endif

/**
 * Update input signals mask
 */
static void power_update_signals(void)
{
	uint32_t inew = 0;
	const struct power_signal_info *s = power_signal_list;
	int i;

	for (i = 0; i < POWER_SIGNAL_COUNT; i++, s++) {
		if (power_signal_is_asserted(s))
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

	CPRINTS("power lost input; wanted 0x%04x, got 0x%04x", want,
		in_signals & want);

	return 0;
}

int power_wait_signals(uint32_t want)
{
	int ret = power_wait_signals_timeout(want, DEFAULT_TIMEOUT);

	if (ret == EC_ERROR_TIMEOUT)
		CPRINTS("power timeout on input; wanted 0x%04x, got 0x%04x",
			want, in_signals & want);
	return ret;
}

int power_wait_signals_timeout(uint32_t want, int timeout)
{
	return power_wait_mask_signals_timeout(want, want, timeout);
}

int power_wait_mask_signals_timeout(uint32_t want, uint32_t mask, int timeout)
{
	in_want = want;
	if (!mask)
		return EC_SUCCESS;

	while ((in_signals & mask) != in_want) {
		if (task_wait_event(timeout) == TASK_EVENT_TIMER) {
			power_update_signals();
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

	/* Print out the RTC value to help correlate EC and kernel logs. */
	print_system_rtc(CC_CHIPSET);

	state = new_state;

	/*
	 * Reset want_g3_exit flag here to prevent the situation that if the
	 * error handler in POWER_S5S4 decides to force shutdown the system and
	 * the flag is set, the system will go to G3 and then immediately exit
	 * G3 again.
	 */
	if ((state == POWER_S5S4) || (state == POWER_S5S3))
		want_g3_exit = 0;
}

enum power_state power_get_state(void)
{
	return state;
}

#ifdef CONFIG_HOSTCMD_X86

/* If host doesn't program s0ix lazy wake mask, use default s0ix mask */
#define DEFAULT_WAKE_MASK_S0IX                        \
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) | \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_MODE_CHANGE))

/*
 * Set the wake mask according to the current power state:
 * 1. On transition to S0, wake mask is reset.
 * 2. In non-S0 states, active mask set by host gets a higher preference.
 * 3. If host has not set any active mask, then check if a lazy mask exists
 *    for the current power state.
 * 4. If state is S0ix and no lazy or active wake mask is set, then use default
 *    S0ix mask to be compatible with older BIOS versions.
 */

void power_update_wake_mask(void)
{
	host_event_t wake_mask;
	enum power_state state;

	state = power_get_state();

	if (state == POWER_S0)
		wake_mask = 0;
	else if (lpc_is_active_wm_set_by_host())
		return;
	else if (get_lazy_wake_mask(state, &wake_mask))
		return;
#ifdef CONFIG_POWER_S0IX
	if ((state == POWER_S0ix) && (wake_mask == 0))
		wake_mask = DEFAULT_WAKE_MASK_S0IX;
#endif

	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, wake_mask);
}
/*
 * Set wake mask after power state has stabilized, 5ms after power state
 * change. The reason for making this a deferred call is to avoid race
 * conditions occurring from S0ix periodic wakes on the SoC.
 */

static void power_update_wake_mask_deferred(void);
DECLARE_DEFERRED(power_update_wake_mask_deferred);

static void power_update_wake_mask_deferred(void)
{
	hook_call_deferred(&power_update_wake_mask_deferred_data, -1);
	power_update_wake_mask();
}

static void power_set_active_wake_mask(void)
{
	/*
	 * Allow state machine to stabilize and update wake mask after 5msec. It
	 * was observed that on platforms where host wakes up periodically from
	 * S0ix for hardware book-keeping activities, there is a small window
	 * where host is not really up and running software, but still SLP_S0#
	 * is de-asserted and hence setting wake mask right away can cause user
	 * wake events to be missed.
	 *
	 * Time for deferred callback was chosen to be 5msec based on the fact
	 * that it takes ~2msec for the periodic wake cycle to complete on the
	 * host for KBL.
	 */
	hook_call_deferred(&power_update_wake_mask_deferred_data, 5 * MSEC);
}

#else
static void power_set_active_wake_mask(void)
{
}
#endif

#ifdef CONFIG_HIBERNATE
#ifdef CONFIG_BATTERY
/*
 * Smart discharge system
 *
 * EC controls how the system discharges differently depending on the remaining
 * capacity and the expected hours to zero.
 *
 * 0          X1                X2                                   full
 * |----------|-------------------|------------------------------------|
 *    cutoff        stay-up                       safe
 *
 * EC cuts off the battery at X1 mAh and hibernates the system at X2 mAh. X1 and
 * X2 are derived from the cutoff and hibernation discharge rate, respectively.
 *
 * TODO: Learn discharge rates dynamically.
 *
 * TODO: Save sdzone in non-volatile memory and restore it when waking up from
 * cutoff or hibernation.
 */
static struct smart_discharge_zone sdzone;

static enum ec_status hc_smart_discharge(struct host_cmd_handler_args *args)
{
	static uint16_t hours_to_zero;
	static struct discharge_rate drate;
	const struct ec_params_smart_discharge *p = args->params;
	struct ec_response_smart_discharge *r = args->response;

	if (p->flags & EC_SMART_DISCHARGE_FLAGS_SET) {
		int cap;

		if (battery_full_charge_capacity(&cap))
			return EC_RES_UNAVAILABLE;

		if (p->drate.hibern < p->drate.cutoff)
			/* Hibernation discharge rate should be always higher */
			return EC_RES_INVALID_PARAM;
		else if (p->drate.cutoff > 0 && p->drate.hibern > 0)
			drate = p->drate;
		else if (p->drate.cutoff == 0 && p->drate.hibern == 0)
			; /* no-op. use the current drate. */
		else
			return EC_RES_INVALID_PARAM;

		/* Commit */
		hours_to_zero = p->hours_to_zero;
		sdzone.stayup = MIN(hours_to_zero * drate.hibern / 1000, cap);
		sdzone.cutoff =
			MIN(hours_to_zero * drate.cutoff / 1000, sdzone.stayup);
	}

	/* Return the effective values. */
	r->hours_to_zero = hours_to_zero;
	r->dzone = sdzone;
	r->drate = drate;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SMART_DISCHARGE, hc_smart_discharge,
		     EC_VER_MASK(0));

__overridable enum critical_shutdown
board_system_is_idle(uint64_t last_shutdown_time, uint64_t *target,
		     uint64_t now)
{
	int remain;

	if (now < *target)
		return CRITICAL_SHUTDOWN_IGNORE;

	if (battery_remaining_capacity(&remain)) {
		CPRINTS("SDC Failed to get remaining capacity");
		return CRITICAL_SHUTDOWN_HIBERNATE;
	}

	if (remain < sdzone.cutoff) {
		CPRINTS("SDC Cutoff");
		return CRITICAL_SHUTDOWN_CUTOFF;
	} else if (remain < sdzone.stayup) {
		CPRINTS("SDC Stay-up");
		return CRITICAL_SHUTDOWN_IGNORE;
	}

	CPRINTS("SDC Safe");
	return CRITICAL_SHUTDOWN_HIBERNATE;
}
#else
/* Default implementation for battery-less systems */
__overridable enum critical_shutdown
board_system_is_idle(uint64_t last_shutdown_time, uint64_t *target,
		     uint64_t now)
{
	return now > *target ? CRITICAL_SHUTDOWN_HIBERNATE :
			       CRITICAL_SHUTDOWN_IGNORE;
}
#endif /* CONFIG_BATTERY */
#endif /* CONFIG_HIBERNATE */

/**
 * Common handler for steady states
 *
 * @param state		Current power state
 * @return Updated power state
 */
static enum power_state power_common_state(void)
{
	switch (state) {
	case POWER_G3:
		if (want_g3_exit || want_reboot_ap_at_g3) {
			uint64_t i;

			want_g3_exit = 0;
			want_reboot_ap_at_g3 = false;
			reboot_ap_at_g3_delay = reboot_ap_at_g3_delay * MSEC;
			/*
			 * G3->S0 transition should happen only after the
			 * user specified delay. Hence, wait until the
			 * user specified delay times out.
			 */
			for (i = 0; i < reboot_ap_at_g3_delay; i += 100)
				crec_msleep(100);
			reboot_ap_at_g3_delay = 0;

			return POWER_G3S5;
		}

		in_want = 0;
#ifdef CONFIG_HIBERNATE
		{
			uint64_t target, now, wait;
			if (extpower_is_present()) {
				task_wait_event(-1);
				break;
			}

			now = get_time().val;
			target = last_shutdown_time +
				 (uint64_t)hibernate_delay * SECOND;
			switch (board_system_is_idle(last_shutdown_time,
						     &target, now)) {
			case CRITICAL_SHUTDOWN_HIBERNATE:
				CPRINTS("Hibernate due to G3 idle");
				system_hibernate(0, 0);
				break;
#ifdef CONFIG_BATTERY_CUT_OFF
			case CRITICAL_SHUTDOWN_CUTOFF:
				CPRINTS("Cutoff due to G3 idle");
				/* Ensure logs are flushed. */
				cflush();
				board_cut_off_battery();
				break;
#endif
			case CRITICAL_SHUTDOWN_IGNORE:
			default:
				break;
			}

			wait = MIN(target - now, TASK_MAX_WAIT_US);
			task_wait_event(wait);
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

		power_wait_signals(0);

		/* Wait for inactivity timeout, if desired */
		if (s5_inactivity_timeout == 0) {
			return POWER_S5G3;
		} else if (s5_inactivity_timeout < 0) {
			task_wait_event(-1);
		} else if (task_wait_event(s5_inactivity_timeout * SECOND) ==
			   TASK_EVENT_TIMER) {
			/* Prepare to drop to G3; wake not requested yet */
			return POWER_S5G3;
		}
		break;

	case POWER_S4:
		__fallthrough;
	case POWER_S3:
		__fallthrough;
	case POWER_S0:
#ifdef CONFIG_POWER_S0IX
		__fallthrough;
	case POWER_S0ix:
#endif
		/* Wait for a message */
		power_wait_signals(0);
		task_wait_event(-1);
		break;

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
	case POWER_S5S4:
	case POWER_S4S5:
	case POWER_S4:
		need_mask = CHIPSET_STATE_SOFT_OFF;
		break;
	case POWER_S5S3:
	case POWER_S3S5:
	case POWER_S4S3:
	case POWER_S3S4:
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

int chipset_in_or_transitioning_to_state(int state_mask)
{
	switch (state) {
	case POWER_G3:
	case POWER_S5G3:
		return state_mask & CHIPSET_STATE_HARD_OFF;
	case POWER_S5:
	case POWER_S4:
	case POWER_S3S5:
	case POWER_G3S5:
	case POWER_S4S5:
	case POWER_S5S4:
	case POWER_S3S4:
		return state_mask & CHIPSET_STATE_SOFT_OFF;
	case POWER_S5S3:
	case POWER_S3:
	case POWER_S4S3:
	case POWER_S0S3:
		return state_mask & CHIPSET_STATE_SUSPEND;
#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
	case POWER_S0S0ix:
		return state_mask & CHIPSET_STATE_STANDBY;
#endif
	case POWER_S0:
	case POWER_S3S0:
#ifdef CONFIG_POWER_S0IX
	case POWER_S0ixS0:
#endif
		return state_mask & CHIPSET_STATE_ON;
	}

	/* Unknown power state; return false. */
	return 0;
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
	task_wake(TASK_ID_CHIPSET);
}

#ifdef CONFIG_ZTEST
void test_power_common_state(void)
{
	enum power_state new_state;

	task_wake(task_get_current());
	new_state = power_common_state();
	if (new_state != state)
		power_set_state(new_state);
}
#endif

/*****************************************************************************/
/* Task function */

void chipset_task(void *u)
{
	enum power_state new_state;
	static enum power_state last_state;
	uint32_t this_in_signals;
	static uint32_t last_in_signals;

	while (1) {
		/*
		 * In order to prevent repeated console spam, only print the
		 * current power state if something has actually changed.  It's
		 * possible that one of the power signals goes away briefly and
		 * comes back by the time we update our in_signals.
		 */
		this_in_signals = in_signals;
		if (this_in_signals != last_in_signals || state != last_state) {
			CPRINTS("power state %d = %s, in 0x%04x", state,
				state_names[state], this_in_signals);
			if (IS_ENABLED(CONFIG_SEVEN_SEG_DISPLAY))
				display_7seg_write(SEVEN_SEG_EC_DISPLAY, state);
			last_in_signals = this_in_signals;
			last_state = state;
		}

		/* Always let the specific chipset handle the state first */
		new_state = power_handle_state(state);

		/*
		 * If the state hasn't changed, run common steady-state
		 * handler.
		 */
		if (new_state == state)
			new_state = power_common_state();

		/* Handle state changes */
		if (new_state != state) {
			power_set_state(new_state);
			power_set_active_wake_mask();

			/* Call hooks before we enter G3 */
			if (new_state == POWER_G3)
				hook_notify(HOOK_CHIPSET_HARD_OFF);
		}
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

	/* Enable interrupts for input signals */
	for (i = 0; i < POWER_SIGNAL_COUNT; i++, s++)
		if (s->flags & POWER_SIGNAL_DISABLE_AT_BOOT)
			power_signal_disable_interrupt(s->gpio);
		else
			power_signal_enable_interrupt(s->gpio);

	/* Call chipset-specific init to set initial state */
	power_set_state(power_chipset_init());

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
#define PTR2IDX(x) ((x) % (MAX_SIGLOG_ENTRIES))

/* circular buffer maintained by head and tail pointers */
static unsigned int siglog_head;
static unsigned int siglog_tail;
static unsigned int siglog_truncated;

static struct {
	timestamp_t time;
	enum gpio_signal signal;
	int level;
} siglog[MAX_SIGLOG_ENTRIES];

static void siglog_deferred(void)
{
	timestamp_t tdiff = { .val = 0 };
	const unsigned int tmp_siglog_head = siglog_head;
	const unsigned int tmp_siglog_tail = siglog_tail;
	const unsigned int tmp_siglog_truncated = siglog_truncated;
	const unsigned int tmp_siglog_entries = (tmp_siglog_tail - siglog_head);

	CPRINTF("%d signal changes:\n", tmp_siglog_entries);
	for (; siglog_head < tmp_siglog_tail; siglog_head++) {
		if (siglog_head != tmp_siglog_head)
			tdiff.val = siglog[PTR2IDX(siglog_head)].time.val -
				    siglog[PTR2IDX(siglog_head - 1)].time.val;
		CPRINTF("  %.6lld  +%.6lld  %s => %d\n",
			siglog[PTR2IDX(siglog_head)].time.val, tdiff.val,
			power_signal_get_name(
				siglog[PTR2IDX(siglog_head)].signal),
			siglog[PTR2IDX(siglog_head)].level);
	}
	if (tmp_siglog_truncated)
		CPRINTF("  SIGNAL LOG TRUNCATED...\n");

	siglog_truncated = 0;
}
DECLARE_DEFERRED(siglog_deferred);

static void siglog_add(enum gpio_signal signal)
{
	const struct power_signal_info *s = power_signal_list;
	int i;
	const unsigned int siglog_entries = siglog_tail - siglog_head;

	for (i = 0; i < POWER_SIGNAL_COUNT; i++, s++) {
		if (s->gpio == signal && s->flags & POWER_SIGNAL_NO_LOG) {
			return;
		}
	}

	if (siglog_entries >= MAX_SIGLOG_ENTRIES) {
		siglog_truncated = 1;
		return;
	}

	siglog[PTR2IDX(siglog_tail)].time = get_time();
	siglog[PTR2IDX(siglog_tail)].signal = signal;
	siglog[PTR2IDX(siglog_tail)].level = power_signal_get_level(signal);
	siglog_tail++;

	hook_call_deferred(&siglog_deferred_data, SECOND);
}

#define SIGLOG(S) siglog_add(S)

#else
#define SIGLOG(S)
#endif /* CONFIG_BRINGUP */

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
DECLARE_HOOK(HOOK_SECOND, reset_power_signal_interrupt_count,
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
				CPRINTS("Interrupt storm! Signal %d", i);
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

static int command_powerinfo(int argc, const char **argv)
{
	/*
	 * Print power state in same format as state machine.  This is
	 * used by FAFT tests, so must match exactly.
	 */
	ccprintf("power state %d = %s, in 0x%04x\n", state, state_names[state],
		 in_signals);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerinfo, command_powerinfo, NULL,
			"Show current power state");

#ifdef CONFIG_CMD_POWERINDEBUG
static int command_powerindebug(int argc, const char **argv)
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
		ccprintf("  0x%04x %d %s\n", mask, in_signals & mask ? 1 : 0,
			 s->name);
	}

	return EC_SUCCESS;
};
DECLARE_CONSOLE_COMMAND(powerindebug, command_powerindebug, "[mask]",
			"Get/set power input debug mask");
#endif

#ifdef CONFIG_CMD_S5_TIMEOUT
/* Allow command-line access to configure our S5 delay for power testing */
static int command_s5_timeout(int argc, const char **argv)
{
	char *e;

	if (argc >= 2) {
		uint32_t s = strtoi(argv[1], &e, 0);

		if (*e)
			return EC_ERROR_PARAM1;

		s5_inactivity_timeout = s;
	}

	/* Print the current setting */
	ccprintf("S5 inactivity timeout: %d s\n", s5_inactivity_timeout);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(s5_timeout, command_s5_timeout, "[sec]",
			"Set the timeout from S5 to G3 transition, "
			"-1 to indicate no transition");
#endif

#ifdef CONFIG_HIBERNATE
static int command_hibernation_delay(int argc, const char **argv)
{
	char *e;
	uint32_t time_g3 =
		((uint32_t)(get_time().val - last_shutdown_time)) / SECOND;

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
DECLARE_CONSOLE_COMMAND(hibdelay, command_hibernation_delay, "[sec]",
			"Set the delay before going into hibernation");

static enum ec_status
host_command_hibernation_delay(struct host_cmd_handler_args *args)
{
	const struct ec_params_hibernation_delay *p = args->params;
	struct ec_response_hibernation_delay *r = args->response;

	uint32_t time_g3;
	uint64_t t = get_time().val - last_shutdown_time;

	uint64divmod(&t, SECOND);
	time_g3 = (uint32_t)t;

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
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HIBERNATION_DELAY, host_command_hibernation_delay,
		     EC_VER_MASK(0));
#endif /* CONFIG_HIBERNATE */

#ifdef CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
static enum ec_status
host_command_pause_in_s5(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_set_value *p = args->params;
	struct ec_response_get_set_value *r = args->response;

	if (p->flags & EC_GSV_SET)
		pause_in_s5 = p->value;

	r->value = pause_in_s5;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GSV_PAUSE_IN_S5, host_command_pause_in_s5,
		     EC_VER_MASK(0));

static int command_pause_in_s5(int argc, const char **argv)
{
	if (argc > 1 && !parse_bool(argv[1], &pause_in_s5))
		return EC_ERROR_INVAL;

	ccprintf("pause_in_s5 = %s\n", pause_in_s5 ? "on" : "off");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pause_in_s5, command_pause_in_s5, "[on|off]",
			"Should the AP pause in S5 during shutdown?");
#endif /* CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5 */

#ifdef CONFIG_POWER_PP5000_CONTROL
__overridable void board_power_5v_enable(int enable)
{
	if (enable)
		gpio_set_level(GPIO_EN_PP5000, 1);
	else
		gpio_set_level(GPIO_EN_PP5000, 0);
}

/* 5V enable request bitmask from various tasks. */
static uint32_t pwr_5v_en_req;
static K_MUTEX_DEFINE(pwr_5v_ctl_mtx);

void power_5v_enable(task_id_t tid, int enable)
{
	mutex_lock(&pwr_5v_ctl_mtx);

	if (enable) /* Set the bit indicating the request. */
		pwr_5v_en_req |= 1 << tid;
	else /* Clear the task's request bit. */
		pwr_5v_en_req &= ~(1 << tid);

	/*
	 * If there are any outstanding requests for the rail to be enabled,
	 * turn on the rail.  Otherwise, turn it off.
	 */
	board_power_5v_enable(pwr_5v_en_req);
	mutex_unlock(&pwr_5v_ctl_mtx);
}

#define P5_SYSJUMP_TAG 0x5005 /* "P5" */
static void restore_enable_5v_state(void)
{
	const uint32_t *state;
	int size;

	state = (const uint32_t *)system_get_jump_tag(P5_SYSJUMP_TAG, 0, &size);
	if (state && size == sizeof(pwr_5v_en_req)) {
		mutex_lock(&pwr_5v_ctl_mtx);
		pwr_5v_en_req |= *state;
		mutex_unlock(&pwr_5v_ctl_mtx);
	}
}
DECLARE_HOOK(HOOK_INIT, restore_enable_5v_state, HOOK_PRIO_FIRST);

static void preserve_enable_5v_state(void)
{
	mutex_lock(&pwr_5v_ctl_mtx);
	system_add_jump_tag(P5_SYSJUMP_TAG, 0, sizeof(pwr_5v_en_req),
			    &pwr_5v_en_req);
	mutex_unlock(&pwr_5v_ctl_mtx);
}
DECLARE_HOOK(HOOK_SYSJUMP, preserve_enable_5v_state, HOOK_PRIO_DEFAULT);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */

#ifdef CONFIG_POWERSEQ_FAKE_CONTROL
static int command_power_fake(int argc, const char **argv)
{
	if (argc < 2) {
		ccprints("Error: Argument required");
		return EC_ERROR_PARAM_COUNT;
	}

	if (strcasecmp(argv[1], "S0") == 0) {
		power_fake_s0();
		if (power_get_state() == POWER_G3)
			want_g3_exit = 1;
	} else if (strcasecmp(argv[1], "disable") == 0) {
		power_fake_disable();
	} else {
		ccprints("Error: Unknown param");
		return EC_ERROR_PARAM1;
	}

	power_update_signals();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerfake, command_power_fake, "S0|disable",
			"Force power inputs for early board bringup");
#endif /* defined(CONFIG_POWERSEQ_FAKE_CONTROL) */
