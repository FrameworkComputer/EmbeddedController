/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button state machine for x86 platforms */

#include "board_adc.h"
#include "board_function.h"
#include "charge_state.h"
#include "customized_shared_memory.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "diagnostics.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power_button.h"
#include "power_sequence.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ##args)

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
 *   PWRBTN#   ---                                          ----
 *     to EC     |__________________________________________|
 *
 *
 *   PWRBTN#   ---  ---------                               ----
 *    to PCH     |__|       |_______________________________|
 *                t0    t1  |    t2    |     t3    |
 *
 *   scan code   |                                          |
 *    to host    v                                          v
 *     @S0   make code                                 break code
 */
#define PWRBTN_DELAY_INIT (5 * MSEC) /* 5ms to wait VALW power rail ready*/
#define PWRBTN_DELAY_T0 (32 * MSEC) /* 32ms (PCH requires >16ms) */
#define PWRBTN_DELAY_T1 (4 * SECOND - PWRBTN_DELAY_T0) /* 4 secs - t0 */
#define PWRBTN_DELAY_T2 (4 * SECOND) /* Force CPU to G3 */
#define PWRBTN_DELAY_T3 (4 * SECOND) /* EC reset */
/*
 * Length of time to stretch initial power button press to give chipset a
 * chance to wake up (~100ms) and react to the press (~16ms).  Also used as
 * pulse length for simulated power button presses when the system is off.
 */
#define PWRBTN_INITIAL_US (200 * MSEC)

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
	/* Power button press keep long time; force shutdown */
	PWRBTN_STATE_NEED_SHUTDOWN,
};
static enum power_button_state pwrbtn_state = PWRBTN_STATE_IDLE;

static const char *const state_names[] = {
	"idle",	    "pressed",	   "t0",      "t1",	  "held",    "lid-open",
	"released", "eat-release", "init-on", "recovery", "was-off", "need-reset",
	"need-shutdown",
};

/*
 * Time for next state transition of power button state machine, or 0 if the
 * state doesn't have a timeout.
 */
static uint64_t tnext_state;

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
		high = 1;
	}
#endif
	gpio_set_level(GPIO_PCH_PWRBTN_L, high);
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
	uint32_t reset_flags = system_get_reset_flags();

	if (reset_flags == EC_RESET_FLAG_HARD) {
		pwrbtn_state = PWRBTN_STATE_INIT_ON;
		CPRINTS("PB init-on after updating firmware");
	} else if (((reset_flags & EC_RESET_FLAG_HIBERNATE) == EC_RESET_FLAG_HIBERNATE) &&
		(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hw_acav_in)) == 0)) {
		/**
		 * EC needs to auto power on after exiting the hibernate mode w/o external power
		 */
		pwrbtn_state = PWRBTN_STATE_INIT_ON;
		CPRINTS("PB init power on");
	} else if (ac_boot_status() &&
		(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hw_acav_in)) == 1)) {
		/* BIOS setup AC attach power on */
		pwrbtn_state = PWRBTN_STATE_INIT_ON;
		CPRINTS("PB init AC attach on");
	} else {
		pwrbtn_state = PWRBTN_STATE_IDLE;
		CPRINTS("PB idle");
	}
}

/**
 * auto power on system when AC plug-in
 */
static void board_extpower(void)
{
	/* AC present to CPU */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ac_present), extpower_is_present());

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
		extpower_is_present() && ac_boot_status()) {
		CPRINTS("Power on from boot on AC present");
		power_button_pch_pulse();
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/**
 * Power button state machine.
 *
 * @param tnow		Current time from usec counter
 */
static void state_machine(uint64_t tnow)
{
	/* Not the time to move onto next state */
	if (tnow < tnext_state)
		return;

	/* States last forever unless otherwise specified */
	tnext_state = 0;

	switch (pwrbtn_state) {
	case PWRBTN_STATE_PRESSED:
		/* chipset exit hard off function only executes at G3 state */
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
			/*
			 * Power button out signal implements in power_sequence.c,
			 * just call the exit hard off start to run the state mechine.
			 */
			reset_diagnostics();
			chipset_exit_hard_off();

			/*
			 * Workaround: the pch now have leakage,
			 * need keep pchbtn to low for while,
			 * if use idle will set to high by release event.
			 *
			 * when HW solved leakage will go back check
			 * should still nedd eat release
			 */
			pwrbtn_state = PWRBTN_STATE_EAT_RELEASE;
		} else {
			/*
			 * when in preOS still need send power button signal
			 * until ACPI driver ready
			 */
			if (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS)
				& ACPI_DRIVER_READY) {
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
		pwrbtn_state = PWRBTN_STATE_HELD;
		break;
	case PWRBTN_STATE_RELEASED:
	case PWRBTN_STATE_LID_OPEN:
		set_pwrbtn_to_pch(1, 0);
		pwrbtn_state = PWRBTN_STATE_IDLE;
		break;
	case PWRBTN_STATE_INIT_ON:

		/*
		 * Before attempting to power the system on, we need to allow
		 * time for charger, battery and USB-C PD initialization to be
		 * ready to supply sufficient power. Check every 100
		 * milliseconds, and give up CONFIG_POWER_BUTTON_INIT_TIMEOUT
		 * seconds after the PB task was started. Here, it is
		 * important to check the current time against PB task start
		 * time to prevent unnecessary timeouts happening in recovery
		 * case where the tasks could start as late as 30 seconds
		 * after EC reset.
		 */

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

		/*
		 * Power button out signal implements in power_sequence.c,
		 * just call the exit hard off start to run the state mechine.
		 */
		reset_diagnostics();
		chipset_exit_hard_off();
		/*
		 * Workaround: the pch now have leakage,
		 * need keep pchbtn to low for while,
		 * if use idle will set to high by release event.
		 *
		 * when HW solved leakage will go back check
		 * should still nedd eat release
		 */
		pwrbtn_state = PWRBTN_STATE_EAT_RELEASE;
		break;
	case PWRBTN_STATE_HELD:

		if (power_button_is_pressed()) {
			tnext_state = tnow + PWRBTN_DELAY_T2;
			pwrbtn_state = PWRBTN_STATE_NEED_SHUTDOWN;
		} else {
			power_button_released(tnow);
		}

		break;
	case PWRBTN_STATE_NEED_SHUTDOWN:

		if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			CPRINTS("PB held press 8s execute force shutdown");
			chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
		}

		if (power_button_is_pressed()) {
			tnext_state = tnow + PWRBTN_DELAY_T3;
			pwrbtn_state = PWRBTN_STATE_NEED_RESET;
		} else
			power_button_released(tnow);

		break;
	case PWRBTN_STATE_NEED_RESET:
		if (power_button_is_pressed())
			system_reset(SYSTEM_RESET_HARD);
		else
			power_button_released(tnow);
		break;
	case PWRBTN_STATE_BOOT_KB_RESET:
	case PWRBTN_STATE_WAS_OFF:
	case PWRBTN_STATE_IDLE:
	case PWRBTN_STATE_EAT_RELEASE:
		/* Do nothing */
		break;
	}
}

void power_button_task(void *u)
{
	uint64_t t;
	uint64_t tsleep;
	enum power_button_state curr_state = PWRBTN_STATE_IDLE;

	/*
	 * Record the time when the task starts so that the state machine can
	 * use this to identify any timeouts.
	 */
	tpb_task_start = get_time().val;

	while (1) {
		t = get_time().val;

		/* Update state machine */
		if (pwrbtn_state != curr_state) {
			CPRINTS("PB task %d = %s", pwrbtn_state,
				state_names[pwrbtn_state]);
			curr_state = pwrbtn_state;
		}

		state_machine(t);

		/* Sleep until our next timeout */
		tsleep = -1;
		if (tnext_state && tnext_state < tsleep)
			tsleep = tnext_state;
		t = get_time().val;
		if (tsleep > t) {
			uint16_t d = tsleep == -1 ? -1 : (uint16_t)(tsleep - t);
			/*
			 * (Yes, the conversion from uint64_t to unsigned could
			 * theoretically overflow if we wanted to sleep for
			 * more than 2^32 us, but our timeouts are small enough
			 * that can't happen - and even if it did, we'd just go
			 * back to sleep after deciding that we woke up too
			 * early.)
			 */
			if (pwrbtn_state != curr_state) {
				CPRINTS("PB task %d = %s, wait %d", pwrbtn_state,
					state_names[pwrbtn_state], d);
			}
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
DECLARE_HOOK(HOOK_INIT, powerbtn_x86_init, HOOK_PRIO_DEFAULT + 1);

void chipset_power_on(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
	    pwrbtn_state != PWRBTN_STATE_INIT_ON) {
		power_button_pch_pulse();
	}
}

#ifdef CONFIG_LID_SWITCH
/**
 * Handle switch changes based on lid event.
 */
static void powerbtn_x86_lid_change(void)
{
	/* If chipset in suspend mode, pulse the power button on lid open to wake it. */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)
	    && pwrbtn_state != PWRBTN_STATE_INIT_ON) {
		chipset_power_on();
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, powerbtn_x86_lid_change, HOOK_PRIO_DEFAULT);
#endif

/**
 * Handle debounced power button changing state.
 */
static void powerbtn_x86_changed(void)
{
	if (pwrbtn_state == PWRBTN_STATE_BOOT_KB_RESET ||
	    pwrbtn_state == PWRBTN_STATE_INIT_ON ||
	    pwrbtn_state == PWRBTN_STATE_LID_OPEN ||
	    pwrbtn_state == PWRBTN_STATE_WAS_OFF) {
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

	return EC_RES_SUCCESS;
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

#define POWER_BUTTON_SYSJUMP_TAG 0x5042 /* PB */
#define POWER_BUTTON_HOOK_VERSION 1

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
	     HOOK_PRIO_POST_POWER_BUTTON);

static void power_button_pulse_setting_preserve_state(void)
{
	system_add_jump_tag(POWER_BUTTON_SYSJUMP_TAG, POWER_BUTTON_HOOK_VERSION,
			    sizeof(power_button_pulse_enabled),
			    &power_button_pulse_enabled);
}
DECLARE_HOOK(HOOK_SYSJUMP, power_button_pulse_setting_preserve_state,
	     HOOK_PRIO_DEFAULT);
