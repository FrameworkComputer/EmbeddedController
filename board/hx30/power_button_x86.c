/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button state machine for x86 platforms */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "board.h"
#include "diagnostics.h"
/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

/*
 * x86 chipsets have a hardware timer on the power button input which causes
 * them to reset when the button is pressed for more than 4 seconds.  This is
 * problematic for Chrome OS, which needs more time than that to transition
 * through the lock and logout screens.  So when the system is on, we need to
 * stretch the power button signal so that the chipset will hard-reboot after 8
 * seconds instead of 4.
 *
 * When the button is pressed, we initially send a short pulse (t0); this
 * allows the chipset to process its initial power button interrupt and do
 * things like wake from suspend.  We then deassert the power button signal to
 * the chipset for (t1 = 4 sec - t0), which keeps the chipset from starting its
 * hard reset timer.  If the power button is still pressed after this period,
 * we again assert the power button signal for the remainder of the press
 * duration.  Since (t0+t1) causes a 4-second offset, the hard reset timeout in
 * the chipset triggers after 8 seconds as desired.
 *
 *   PWRBTN#   ---                      ----
 *     to EC     |______________________|
 *
 *
 *   PWRBTN#   ---  ---------           ----
 *    to PCH     |__|       |___________|
 *                t0    t1    held down
 *
 *   scan code   |                      |
 *    to host    v                      v
 *     @S0   make code             break code
 */
#define PWRBTN_DELAY_T0    (32 * MSEC)  /* 32ms (PCH requires >16ms) */
#define PWRBTN_DELAY_T1    (4 * SECOND - PWRBTN_DELAY_T0)  /* 4 secs - t0 */
#define PWRBTN_DELAY_T4 \
	(8 * SECOND - PWRBTN_DELAY_T1 - PWRBTN_DELAY_T0)  /* 8 secs - t1 */
#define PWRBTN_DELAY_T2 \
	(20 * SECOND - PWRBTN_DELAY_T4 - PWRBTN_DELAY_T1)  /* 20 secs - t4 */
#define PWRBTN_DELAY_T3 \
	(10 * SECOND - PWRBTN_DELAY_T4 - PWRBTN_DELAY_T1)  /* 10 secs - t4 */
/*
 * Length of time to stretch initial power button press to give chipset a
 * chance to wake up (~100ms) and react to the press (~16ms).  Also used as
 * pulse length for simulated power button presses when the system is off.
 */
#define PWRBTN_INITIAL_US  (200 * MSEC)
#define PWRBTN_WAS_OFF_DEBOUNCE (500 * MSEC) /* power button man-made bounce */
#define PWRBTN_WAIT_RSMRST (20 * MSEC)
#define PWRBTN_DELAY_INITIAL	(100 * MSEC)
#define PWRBTN_RETRY_COUNT  200				 /* base on PWRBTN_WAIT_RSMRST 1 count = 20ms */
#define PWRBTN_WAIT_RELEASE (100 * MSEC)
#define PWRBTN_STATE_DELAY  (1 * MSEC)       /* debounce for the state change */

enum power_button_state {
	/* Button up; state machine idle */
	PWRBTN_STATE_IDLE = 0,
	/* Button pressed; debouncing done */
	PWRBTN_STATE_PRESSED,
	/* Button down, chipset on; sending initial short pulse */
	PWRBTN_STATE_T0,
	/* Button down, chipset on; delaying until we should reassert signal */
	PWRBTN_STATE_T1,
	/* Button down, signal asserted to chipset */
	PWRBTN_STATE_HELD,
	/* Force pulse due to lid-open event */
	PWRBTN_STATE_LID_OPEN,
	/* Button released; debouncing done */
	PWRBTN_STATE_RELEASED,
	/* Ignore next button release */
	PWRBTN_STATE_EAT_RELEASE,
	/*
	 * Need to power on system after init, but waiting to find out if
	 * sufficient battery power.
	 */
	PWRBTN_STATE_INIT_ON,
	/* Forced pulse at EC boot due to keyboard controlled reset */
	PWRBTN_STATE_BOOT_KB_RESET,
	/* Power button pressed when chipset was off; stretching pulse */
	PWRBTN_STATE_WAS_OFF,
	/* Power button pressed keep long time; reset EC */
	PWRBTN_STATE_NEED_RESET,
	/* Power button pressed keep long time; battery disconnect */
	PWRBTN_STATE_NEED_BATT_CUTOFF,
	/* Power button press keep long time; force shutdown */
	PWRBTN_STATE_NEED_SHUTDOWN,
};
static enum power_button_state pwrbtn_state = PWRBTN_STATE_IDLE;

static const char * const state_names[] = {
	"idle",
	"pressed",
	"t0",
	"t1",
	"held",
	"lid-open",
	"released",
	"eat-release",
	"init-on",
	"recovery",
	"was-off",
	"need-reset",
	"batt-cutoff",
	"force-shutdown"
};

/*
 * Time for next state transition of power button state machine, or 0 if the
 * state doesn't have a timeout.
 */
static uint64_t tnext_state;

/* retry for the PWRBTN_WAIT_RSMRST */
static int rsmrst_retry = 0;

/*
 * Record the time when power button task starts. It can be used by any code
 * path that needs to compare the current time with power button task start time
 * to identify any timeouts e.g. PB state machine checks current time to
 * identify if it should wait more for charger and battery to be initialized. In
 * case of recovery using buttons (where the user could be holding the buttons
 * for >30seconds), it is not right to compare current time with the time when
 * EC was reset since the tasks would not have started. Hence, this variable is
 * being added to record the time at which power button task starts.
 */
static uint64_t tpb_task_start;

/*
 * Determines whether to execute power button pulse (t0 stage)
 */
static int power_button_pulse_enabled = 1;


static int power_button_battery_cutoff;

static void set_pwrbtn_to_pch(int high, int init)
{
	/*
	 * If the battery is discharging and low enough we'd shut down the
	 * system, don't press the power button. Also, don't press the
	 * power button if the battery is charging but the battery level
	 * is too low.
	 */
#ifdef CONFIG_CHARGER
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) && !high &&
		(charge_want_shutdown() || charge_prevent_power_on(!init))) {
		CPRINTS("PB PCH pwrbtn ignored due to battery level");
		update_prevent_power_on_flag(1);
		high = 1;
	}
#endif
	CPRINTS("PB PCH pwrbtn=%s", high ? "HIGH" : "LOW");
	if (IS_ENABLED(CONFIG_POWER_BUTTON_TO_PCH_CUSTOM))
		board_pwrbtn_to_pch(high);
	else
		gpio_set_level(GPIO_PCH_PWRBTN_L, high);
}

void power_button_pch_press(void)
{
	CPRINTS("PB PCH force press");

	/* Assert power button signal to PCH */
	if (!power_button_is_pressed())
		set_pwrbtn_to_pch(0, 0);
}

void power_button_pch_release(void)
{
	CPRINTS("PB PCH force release");

	/* Deassert power button signal to PCH */
	set_pwrbtn_to_pch(1, 0);

	/*
	 * If power button is actually pressed, eat the next release so we
	 * don't send an extra release.
	 */
	if (power_button_is_pressed())
		pwrbtn_state = PWRBTN_STATE_EAT_RELEASE;
	else
		pwrbtn_state = PWRBTN_STATE_IDLE;
}

void power_button_pch_pulse(void)
{
	CPRINTS("PB PCH pulse");

	chipset_exit_hard_off();
	set_pwrbtn_to_pch(0, 0);
	pwrbtn_state = PWRBTN_STATE_LID_OPEN;
	tnext_state = get_time().val + PWRBTN_INITIAL_US;
	task_wake(TASK_ID_POWERBTN);
}

__override void power_button_simulate_press(void)
{
	CPRINTS("Simulation PB press");

	chipset_exit_hard_off();
	pwrbtn_state = PWRBTN_STATE_PRESSED;
	tnext_state = get_time().val + PWRBTN_INITIAL_US;
	task_wake(TASK_ID_POWERBTN);
}

/**
 * Handle debounced power button down.
 */
static void power_button_pressed(uint64_t tnow)
{
	CPRINTS("PB pressed");
	pwrbtn_state = PWRBTN_STATE_PRESSED;
	tnext_state = tnow;
}

/**
 * Handle debounced power button up.
 */
static void power_button_released(uint64_t tnow)
{
	CPRINTS("PB released");
	pwrbtn_state = PWRBTN_STATE_RELEASED;
	tnext_state = tnow;
}

/**
 * Set initial power button state.
 */
static void set_initial_pwrbtn_state(void)
{
	pwrbtn_state = PWRBTN_STATE_INIT_ON;
	CPRINTS("PB init-on");
}

int power_button_batt_cutoff(void)
{
	return power_button_battery_cutoff;
}

/**
 * Power button state machine.
 *
 * @param tnow		Current time from usec counter
 */
static void state_machine(uint64_t tnow)
{
	static int initial_delay = 7; /* 700 ms */
	static int retry_wait = 0;

	/* Not the time to move onto next state */
	if (tnow < tnext_state)
		return;

	/* States last forever unless otherwise specified */
	tnext_state = 0;

	switch (pwrbtn_state) {
	case PWRBTN_STATE_PRESSED:
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			/*
			 * Chipset is off, so wake the chipset and send it a
			 * long enough pulse to wake up.  After that we'll
			 * reflect the true power button state.  If we don't
			 * stretch the pulse here, the user may release the
			 * power button before the chipset finishes waking from
			 * hard off state.
			 */
			reset_diagnostics();
			chipset_exit_hard_off();

			if (!gpio_get_level(GPIO_PCH_RSMRST_L)) {
				tnext_state = tnow + PWRBTN_WAIT_RSMRST;
				CPRINTS("BTN wait RSMRST to asserted");

				/* if RSMRST keep low 4s jump state to T1 */
				if (++rsmrst_retry < PWRBTN_RETRY_COUNT)
					break;

				tnext_state = tnow + PWRBTN_STATE_DELAY;
				pwrbtn_state = PWRBTN_STATE_T1;
			}

			retry_wait = PWRBTN_DELAY_T1 - (rsmrst_retry * PWRBTN_WAIT_RSMRST);
			rsmrst_retry = 0;

			tnext_state = tnow + PWRBTN_WAS_OFF_DEBOUNCE;
			pwrbtn_state = PWRBTN_STATE_WAS_OFF;
			msleep(20);
			set_pwrbtn_to_pch(0, 0);
		} else {
			/*
			 * when in preOS still need send power button signal
			 * until ACPI driver ready
			 */
			if (pos_get_state()) {
				/*
				 * When chipset is on and ACPI driver ready,
				 * we will send the SCI event to trigger modern standby.
				 */
				tnext_state = tnow + PWRBTN_DELAY_T1;
				pwrbtn_state = PWRBTN_STATE_T1;
			} else {
				tnext_state = tnow + PWRBTN_DELAY_T0;
				pwrbtn_state = PWRBTN_STATE_T0;
				set_pwrbtn_to_pch(0, 0);
				cancel_diagnostics();
			}
		}
		break;
	case PWRBTN_STATE_T0:
		tnext_state = tnow + PWRBTN_DELAY_T1;
		pwrbtn_state = PWRBTN_STATE_T1;
		set_pwrbtn_to_pch(1, 0);
		break;
	case PWRBTN_STATE_T1:
		/*
		 * If the chipset is already off, don't tell it the power
		 * button is down; it'll just cause the chipset to turn on
		 * again.
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			CPRINTS("PB chipset already off");
		else
			set_pwrbtn_to_pch(0, 0);

		tnext_state = tnow + PWRBTN_STATE_DELAY;
		pwrbtn_state = PWRBTN_STATE_HELD;
		break;
	case PWRBTN_STATE_RELEASED:
	case PWRBTN_STATE_LID_OPEN:
		set_pwrbtn_to_pch(1, 0);
		pwrbtn_state = PWRBTN_STATE_IDLE;
		break;
	case PWRBTN_STATE_INIT_ON:

		/*
		if (!IS_ENABLED(CONFIG_CHARGER) || charge_prevent_power_on(0)) {
			if (tnow >
				(tpb_task_start +
				 CONFIG_POWER_BUTTON_INIT_TIMEOUT * SECOND)) {
				pwrbtn_state = PWRBTN_STATE_IDLE;
				break;
			}

			if (IS_ENABLED(CONFIG_CHARGER)) {
				tnext_state = tnow + 100 * MSEC;
				break;
			}
		}
		*/

		/* if trigger by power button don't need wait initial.*/
		if (power_button_is_pressed() || poweron_reason_powerbtn()) {
			initial_delay = 0;
		}

		if (initial_delay != 0) {
			tnext_state = tnow + PWRBTN_DELAY_INITIAL;
			initial_delay--;
		} else {
			if (poweron_reason_powerbtn() || poweron_reason_acin() ||
			 ((system_get_reset_flags() & EC_RESET_FLAG_HARD) == EC_RESET_FLAG_HARD)) {

				reset_diagnostics();

				if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
					chipset_exit_hard_off();

				/* Need to wait RSMRST signal then asserted BTN */
				if (!gpio_get_level(GPIO_PCH_RSMRST_L)) {
					/* TODO: need to add the retry limit */
					tnext_state = tnow + PWRBTN_WAIT_RSMRST;
					CPRINTS("BTN wait RSMRST to asserted (INIT)");
					break;
				}
				msleep(20);
				set_pwrbtn_to_pch(0, 1);
			}

			tnext_state = get_time().val + PWRBTN_INITIAL_US;
			pwrbtn_state = PWRBTN_STATE_BOOT_KB_RESET;
		}


		break;

	case PWRBTN_STATE_BOOT_KB_RESET:
		/* Initial forced pulse is done.  Ignore the actual power
		 * button until it's released, so that holding down the
		 * recovery combination doesn't cause the chipset to shut back
		 * down. */
		if (poweron_reason_powerbtn() || poweron_reason_acin() ||
			((system_get_reset_flags() & EC_RESET_FLAG_HARD) == EC_RESET_FLAG_HARD))
			set_pwrbtn_to_pch(1, 1);

		if (power_button_is_pressed())
			pwrbtn_state = PWRBTN_STATE_EAT_RELEASE;
		else
			pwrbtn_state = PWRBTN_STATE_IDLE;
		break;
	case PWRBTN_STATE_WAS_OFF:
		/* Done stretching initial power button signal, so show the
		 * true power button state to the PCH. */
		if (power_button_is_pressed()) {
			/* User is still holding the power button */
			tnext_state = tnow + (retry_wait - PWRBTN_WAS_OFF_DEBOUNCE);
			pwrbtn_state = PWRBTN_STATE_HELD;
		} else {
			/* Stop stretching the power button press */
			power_button_released(tnow);
		}
		break;
	case PWRBTN_STATE_IDLE:
		/* Do nothing */
		break;
	case PWRBTN_STATE_HELD:

		if (power_button_is_pressed()) {
			tnext_state = tnow + PWRBTN_DELAY_T4;
			pwrbtn_state = PWRBTN_STATE_NEED_SHUTDOWN;
		} else {
			power_button_released(tnow);
		}

		break;
	case PWRBTN_STATE_EAT_RELEASE:
		/* Do nothing */
		break;
	case PWRBTN_STATE_NEED_BATT_CUTOFF:
		if (power_button_is_pressed()) {
			power_button_battery_cutoff = 1;
			/* User is still holding the power button */
			tnext_state = tnow + PWRBTN_WAIT_RELEASE;
			pwrbtn_state = PWRBTN_STATE_NEED_BATT_CUTOFF;
			CPRINTS("wait release PB");
		} else {
			power_button_battery_cutoff = 0;
			board_cut_off_battery();
			CPRINTS("PB held press 10s execute battery disconnect");
			power_button_released(tnow);
		}
		break;
	case PWRBTN_STATE_NEED_RESET:
		if (power_button_is_pressed()) {
			CPRINTS("PB held press 20s execute chip reset");
			system_reset(SYSTEM_RESET_HARD);
		} else {
			/* Stop stretching the power button press */
			power_button_released(tnow);
		}
		break;
	case PWRBTN_STATE_NEED_SHUTDOWN:

		if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			CPRINTS("PB held press 8s execute force shutdown");
			chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
		}

		if (power_button_is_pressed()) {
			if (!gpio_get_level(GPIO_ON_OFF_FP_L)) {
				tnext_state = tnow + PWRBTN_DELAY_T2;
				pwrbtn_state = PWRBTN_STATE_NEED_RESET;
			} else if (!gpio_get_level(GPIO_ON_OFF_BTN_L)) {
				tnext_state = tnow + PWRBTN_DELAY_T3;
				pwrbtn_state = PWRBTN_STATE_NEED_BATT_CUTOFF;
			}
		} else {
			power_button_released(tnow);
		}

		break;
	}
}

void power_button_task(void *u)
{
	uint64_t t;
	uint64_t tsleep;

	/*
	 * Record the time when the task starts so that the state machine can
	 * use this to identify any timeouts.
	 */
	tpb_task_start = get_time().val;

	while (1) {
		t = get_time().val;

		/* Update state machine */
		CPRINTS("PB task %d = %s", pwrbtn_state,
			state_names[pwrbtn_state]);

		state_machine(t);

		/* Sleep until our next timeout */
		tsleep = -1;
		if (tnext_state && tnext_state < tsleep)
			tsleep = tnext_state;
		t = get_time().val;
		if (tsleep > t) {
			unsigned d = tsleep == -1 ? -1 : (unsigned)(tsleep - t);
			/*
			 * (Yes, the conversion from uint64_t to unsigned could
			 * theoretically overflow if we wanted to sleep for
			 * more than 2^32 us, but our timeouts are small enough
			 * that can't happen - and even if it did, we'd just go
			 * back to sleep after deciding that we woke up too
			 * early.)
			 */
			CPRINTS("PB task %d = %s, wait %d", pwrbtn_state,
			state_names[pwrbtn_state], d);
			task_wait_event(d);
		}
	}
}

/*****************************************************************************/
/* Hooks */

static void powerbtn_x86_init(void)
{
	set_initial_pwrbtn_state();
}
DECLARE_HOOK(HOOK_INIT, powerbtn_x86_init, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_LID_SWITCH
/**
 * Handle switch changes based on lid event.
 */
static void powerbtn_x86_lid_change(void)
{
	/* If chipset is s3 or s0ix, pulse the power button on lid open to wake it. */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)
	    && pwrbtn_state != PWRBTN_STATE_INIT_ON)
		power_button_pch_pulse();
}
DECLARE_HOOK(HOOK_LID_CHANGE, powerbtn_x86_lid_change, HOOK_PRIO_DEFAULT);
#endif

/**
 * Handle debounced power button changing state.
 */
static void powerbtn_x86_changed(void)
{
	/*
	* clear VCI button register before shutdown to avoid
	* AC only will autoboot problem.
	*/
	if (!power_button_is_pressed()) {
		MCHP_VCI_NEGEDGE_DETECT = BIT(0) |  BIT(1);
		MCHP_VCI_POSEDGE_DETECT = BIT(0) |  BIT(1);

	}

	if (pwrbtn_state == PWRBTN_STATE_BOOT_KB_RESET ||
	    pwrbtn_state == PWRBTN_STATE_INIT_ON ||
	    pwrbtn_state == PWRBTN_STATE_LID_OPEN ||
	    pwrbtn_state == PWRBTN_STATE_WAS_OFF ||
		pwrbtn_state == PWRBTN_STATE_NEED_BATT_CUTOFF) {
		/* Ignore all power button changes during an initial pulse */
		CPRINTS("PB ignoring change");
		return;
	}

	if (power_button_is_pressed()) {
		/* Power button pressed */
		power_button_pressed(get_time().val);
	} else {
		/* Power button released */
		if (pwrbtn_state == PWRBTN_STATE_EAT_RELEASE) {
			/*
			 * Ignore the first power button release if we already
			 * told the PCH the power button was released.
			 */
			CPRINTS("PB ignoring release");
			pwrbtn_state = PWRBTN_STATE_IDLE;
			return;
		}
		/* if system is in G3 or S5 will run to was off state to released button */
		if (!chipset_in_state(CHIPSET_STATE_ANY_OFF))
			power_button_released(get_time().val);

	}

	/* Wake the power button task */
	task_wake(TASK_ID_POWERBTN);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbtn_x86_changed, HOOK_PRIO_DEFAULT);

/**
 * Handle configuring the power button behavior through a host command
 */
static enum ec_status hc_config_powerbtn_x86(struct host_cmd_handler_args *args)
{
	const struct ec_params_config_power_button *p = args->params;

	power_button_pulse_enabled =
		!!(p->flags & EC_POWER_BUTTON_ENABLE_PULSE);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CONFIG_POWER_BUTTON, hc_config_powerbtn_x86,
		     EC_VER_MASK(0));


/*
 * Currently, the only reason why we disable power button pulse is to allow
 * detachable menu on AP to use power button for selection purpose without
 * triggering SMI. Thus, re-enable the pulse any time there is a chipset
 * state transition event.
 */
static void power_button_pulse_setting_reset(void)
{
	power_button_pulse_enabled = 1;
}

DECLARE_HOOK(HOOK_CHIPSET_STARTUP, power_button_pulse_setting_reset,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, power_button_pulse_setting_reset,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, power_button_pulse_setting_reset,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, power_button_pulse_setting_reset,
	     HOOK_PRIO_DEFAULT);

#define POWER_BUTTON_SYSJUMP_TAG		0x5042 /* PB */
#define POWER_BUTTON_HOOK_VERSION		1

static void power_button_pulse_setting_restore_state(void)
{
	const int *state;
	int version, size;

	state = (const int *)system_get_jump_tag(POWER_BUTTON_SYSJUMP_TAG,
						 &version, &size);

	if (state && (version == POWER_BUTTON_HOOK_VERSION) &&
	    (size == sizeof(power_button_pulse_enabled)))
		power_button_pulse_enabled = *state;
}
DECLARE_HOOK(HOOK_INIT, power_button_pulse_setting_restore_state,
	     HOOK_PRIO_INIT_POWER_BUTTON + 1);

static void power_button_pulse_setting_preserve_state(void)
{
	system_add_jump_tag(POWER_BUTTON_SYSJUMP_TAG,
			    POWER_BUTTON_HOOK_VERSION,
			    sizeof(power_button_pulse_enabled),
			    &power_button_pulse_enabled);
}
DECLARE_HOOK(HOOK_SYSJUMP, power_button_pulse_setting_preserve_state,
	     HOOK_PRIO_DEFAULT);
