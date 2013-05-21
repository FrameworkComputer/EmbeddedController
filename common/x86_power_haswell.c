/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power_button.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "x86_power.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

/*
 * Default timeout in us; if we've been waiting this long for an input
 * transition, just jump to the next state.
 */
#define DEFAULT_TIMEOUT SECOND

/* Timeout for dropping back from S5 to G3 */
#define S5_INACTIVITY_TIMEOUT (10 * SECOND)

enum x86_state {
	X86_G3 = 0,                 /*
				     * System is off (not technically all the
				     * way into G3, which means totally
				     * unpowered...)
				     */
	X86_S5,                     /* System is soft-off */
	X86_S3,                     /* Suspend; RAM on, processor is asleep */
	X86_S0,                     /* System is on */

	/* Transitions */
	X86_G3S5,                   /* G3 -> S5 (at system init time) */
	X86_S5S3,                   /* S5 -> S3 */
	X86_S3S0,                   /* S3 -> S0 */
	X86_S0S3,                   /* S0 -> S3 */
	X86_S3S5,                   /* S3 -> S5 */
	X86_S5G3,                   /* S5 -> G3 */
};

static const char * const state_names[] = {
	"G3",
	"S5",
	"S3",
	"S0",
	"G3->S5",
	"S5->S3",
	"S3->S0",
	"S0->S3",
	"S3->S5",
	"S5->G3",
};

/* Input state flags */
#define IN_PGOOD_PP5000            0x0001
#define IN_PGOOD_PP1350            0x0002
#define IN_PGOOD_PP1050            0x0004
#define IN_PGOOD_VCORE             0x0008
#define IN_PCH_SLP_S0n_DEASSERTED  0x0010
#define IN_PCH_SLP_S3n_DEASSERTED  0x0020
#define IN_PCH_SLP_S5n_DEASSERTED  0x0040
#define IN_PCH_SLP_SUSn_DEASSERTED 0x0080
#define IN_PCH_SUSWARNn_DEASSERTED 0x0100

/* All always-on supplies */
#define IN_PGOOD_ALWAYS_ON   (IN_PGOOD_PP5000)
/* All non-core power rails */
#define IN_PGOOD_ALL_NONCORE (IN_PGOOD_PP1350 | IN_PGOOD_PP1050)
/* All core power rails */
#define IN_PGOOD_ALL_CORE    (IN_PGOOD_VCORE)
/* Rails required for S3 */
#define IN_PGOOD_S3          (IN_PGOOD_ALWAYS_ON | IN_PGOOD_PP1350)
/* Rails required for S0 */
#define IN_PGOOD_S0          (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE)

/* All PM_SLP signals from PCH deasserted */
#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3n_DEASSERTED |		\
				  IN_PCH_SLP_S5n_DEASSERTED)
/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE |		\
		   IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

static enum x86_state state = X86_G3;  /* Current state */
static uint32_t in_signals;   /* Current input signal states (IN_PGOOD_*) */
static uint32_t in_want;      /* Input signal state we're waiting for */
static uint32_t in_debug;     /* Signal values which print debug output */
static int want_g3_exit;      /* Should we exit the G3 state? */
static int throttle_cpu;      /* Throttle CPU? */

/* When did we enter G3? */
static uint64_t last_shutdown_time;
/* Delay before go into hibernation in seconds*/
static uint32_t hibernate_delay = 3600; /* 1 Hour */

/**
 * Update input signal state.
 */
static void update_in_signals(void)
{
	uint32_t inew = 0;
	int v;

	if (gpio_get_level(GPIO_PP5000_PGOOD))
		inew |= IN_PGOOD_PP5000;
	if (gpio_get_level(GPIO_PP1350_PGOOD))
		inew |= IN_PGOOD_PP1350;
	if (gpio_get_level(GPIO_PP1050_PGOOD))
		inew |= IN_PGOOD_PP1050;
	if (gpio_get_level(GPIO_VCORE_PGOOD))
		inew |= IN_PGOOD_VCORE;

	if (gpio_get_level(GPIO_PCH_SLP_S0_L))
		inew |= IN_PCH_SLP_S0n_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_S3_L))
		inew |= IN_PCH_SLP_S3n_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_S5_L))
		inew |= IN_PCH_SLP_S5n_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_SUS_L))
		inew |= IN_PCH_SLP_SUSn_DEASSERTED;

	v = gpio_get_level(GPIO_PCH_SUSWARN_L);
	if (v)
		inew |= IN_PCH_SUSWARNn_DEASSERTED;
	/* Copy SUSWARN# signal from PCH to SUSACK# */
	gpio_set_level(GPIO_PCH_SUSACK_L, v);

	if ((in_signals & in_debug) != (inew & in_debug))
		CPRINTF("[%T x86 in 0x%04x]\n", inew);

	in_signals = inew;
}

/**
 * Check for required inputs
 *
 * @param want		Input flags which must be present (IN_*)
 *
 * @return Non-zero if all present; zero if a required signal is missing.
 */
static int have_all_in_signals(uint32_t want)
{
	if ((in_signals & want) == want)
		return 1;

	CPRINTF("[%T x86 power lost input; wanted 0x%04x, got 0x%04x]\n",
		want, in_signals & want);

	return 0;
}

/**
 * Wait for inputs to be present
 *
 * @param want		Input flags which must be present (IN_*)
 *
 * @return EC_SUCCESS when all inputs are present, or ERROR_TIMEOUT if timeout
 * before reaching the desired state.
 */
static int wait_in_signals(uint32_t want)
{
	in_want = want;

	while ((in_signals & in_want) != in_want) {
		if (task_wait_event(DEFAULT_TIMEOUT) == TASK_EVENT_TIMER) {
			update_in_signals();
			CPRINTF("[%T x86 power timeout on input; "
				"wanted 0x%04x, got 0x%04x]\n",
				in_want, in_signals & in_want);
			return EC_ERROR_TIMEOUT;
		}
		/*
		 * TODO: should really shrink the remaining timeout if we woke
		 * up but didn't have all the signals we wanted.  Also need to
		 * handle aborts if we're no longer in the same state we were
		 * when we started waiting.
		 */
	}
	return EC_SUCCESS;
}

/*****************************************************************************/
/* Chipset interface */

void chipset_force_shutdown(void)
{
	CPRINTF("[%T %s()]\n", __func__);

	/*
	 * Force x86 off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	gpio_set_level(GPIO_PCH_DPWROK, 0);
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
}

void chipset_reset(int cold_reset)
{
	CPRINTF("[%T %s(%d)]\n", __func__, cold_reset);
	if (cold_reset) {
		/*
		 * Drop and restore PWROK.  This causes the PCH to reboot,
		 * regardless of its after-G3 setting.  This type of reboot
		 * causes the PCH to assert PLTRST#, SLP_S3#, and SLP_S5#, so
		 * we actually drop power to the rest of the system (hence, a
		 * "cold" reboot).
		 */

		/* Ignore if PWROK is already low */
		if (gpio_get_level(GPIO_PCH_PWROK) == 0)
			return;

		/* PWROK must deassert for at least 3 RTC clocks = 91 us */
		gpio_set_level(GPIO_PCH_PWROK, 0);
		udelay(100);
		gpio_set_level(GPIO_PCH_PWROK, 1);

	} else {
		/*
		 * Send a RCIN# pulse to the PCH.  This just causes it to
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system.
		 */

		/*
		 * Pulse must be at least 16 PCI clocks long = 500 ns. The gpio
		 * pin used by the EC is configured as open drain. Therefore,
		 * the driving RCIN# low needs to the level 1 to enable the
		 * FET and 0 to disable the FET. */
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
	}
}

int chipset_in_state(int state_mask)
{
	int need_mask = 0;

	/*
	 * TODO: what to do about state transitions?  If the caller wants
	 * HARD_OFF|SOFT_OFF and we're in G3S5, we could still return
	 * non-zero.
	 */
	switch (state) {
	case X86_G3:
		need_mask = CHIPSET_STATE_HARD_OFF;
		break;
	case X86_G3S5:
	case X86_S5G3:
		/*
		 * In between hard and soft off states.  Match only if caller
		 * will accept both.
		 */
		need_mask = CHIPSET_STATE_HARD_OFF | CHIPSET_STATE_SOFT_OFF;
		break;
	case X86_S5:
		need_mask = CHIPSET_STATE_SOFT_OFF;
		break;
	case X86_S5S3:
	case X86_S3S5:
		need_mask = CHIPSET_STATE_SOFT_OFF | CHIPSET_STATE_SUSPEND;
		break;
	case X86_S3:
		need_mask = CHIPSET_STATE_SUSPEND;
		break;
	case X86_S3S0:
	case X86_S0S3:
		need_mask = CHIPSET_STATE_SUSPEND | CHIPSET_STATE_ON;
		break;
	case X86_S0:
		need_mask = CHIPSET_STATE_ON;
		break;
	}

	/* Return non-zero if all needed bits are present */
	return (state_mask & need_mask) == need_mask;
}

void chipset_exit_hard_off(void)
{
	/* If not in the hard-off state nor headed there, nothing to do */
	if (state != X86_G3 && state != X86_S5G3)
		return;

	/* Set a flag to leave G3, then wake the task */
	want_g3_exit = 1;

	if (task_start_called())
		task_wake(TASK_ID_CHIPSET);
}

void chipset_throttle_cpu(int throttle)
{
	/* FIXME CPRINTF("[%T %s(%d)]\n", __func__, throttle);*/
}

/*****************************************************************************/
/* Hooks */

static void x86_lid_change(void)
{
	/* Wake up the task to update power state */
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_LID_CHANGE, x86_lid_change, HOOK_PRIO_DEFAULT);

static void x86_power_ac_change(void)
{
	if (extpower_is_present()) {
		CPRINTF("[%T x86 AC on]\n");
	} else {
		CPRINTF("[%T x86 AC off]\n");

		if (state == X86_G3) {
			last_shutdown_time = get_time().val;
			task_wake(TASK_ID_CHIPSET);
		}
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, x86_power_ac_change, HOOK_PRIO_DEFAULT);

static void x86_power_init(void)
{
	/* Update input state */
	update_in_signals();
	in_want = 0;

	/* The initial state is G3. Set shut down timestamp to now. */
	last_shutdown_time = get_time().val;

	/*
	 * If we're switching between images without rebooting, see if the x86
	 * is already powered on; if so, leave it there instead of cycling
	 * through G3.
	 */
	if (system_jumped_to_this_image()) {
		if ((in_signals & IN_ALL_S0) == IN_ALL_S0) {
			CPRINTF("[%T x86 already in S0]\n");
			state = X86_S0;
		} else {
			/* Force all signals to their G3 states */
			CPRINTF("[%T x86 forcing G3]\n");
			gpio_set_level(GPIO_PCH_PWROK, 0);
			gpio_set_level(GPIO_VCORE_EN, 0);
			gpio_set_level(GPIO_SUSP_VR_EN, 0);
			gpio_set_level(GPIO_PP1350_EN, 0);
			gpio_set_level(GPIO_EC_EDP_VDD_EN, 0);
			gpio_set_level(GPIO_PP3300_DX_EN, 0);
			gpio_set_level(GPIO_PP3300_WLAN_EN, 0);
			gpio_set_level(GPIO_PP5000_EN, 0);
			gpio_set_level(GPIO_PCH_RSMRST_L, 0);
			gpio_set_level(GPIO_PCH_DPWROK, 0);
		}
	}

	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_LID_OPEN);
	gpio_enable_interrupt(GPIO_AC_PRESENT);
	gpio_enable_interrupt(GPIO_PCH_BKLTEN);
	gpio_enable_interrupt(GPIO_PCH_SLP_S0_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_S3_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_S5_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_SUS_L);
	gpio_enable_interrupt(GPIO_PCH_SUSWARN_L);
	gpio_enable_interrupt(GPIO_PP1050_PGOOD);
	gpio_enable_interrupt(GPIO_PP1350_PGOOD);
	gpio_enable_interrupt(GPIO_PP5000_PGOOD);
	gpio_enable_interrupt(GPIO_VCORE_PGOOD);
	gpio_enable_interrupt(GPIO_PCH_EDP_VDD_EN);
}
DECLARE_HOOK(HOOK_INIT, x86_power_init, HOOK_PRIO_INIT_CHIPSET);

/*****************************************************************************/
/* Interrupts */

void x86_power_interrupt(enum gpio_signal signal)
{
	/* Shadow signals and compare with our desired signal state. */
	update_in_signals();

	/* Pass through eDP VDD enable from PCH. Put this on own interrupt? */
	if (gpio_get_level(GPIO_PCH_EDP_VDD_EN))
		gpio_set_level(GPIO_EC_EDP_VDD_EN, 1);
	else
		gpio_set_level(GPIO_EC_EDP_VDD_EN, 0);

	/* Wake up the task */
	task_wake(TASK_ID_CHIPSET);
}

/*****************************************************************************/
/* Task function */

void chipset_task(void)
{
	uint64_t time_now;

	while (1) {
		CPRINTF("[%T x86 power state %d = %s, in 0x%04x]\n",
			state, state_names[state], in_signals);

		switch (state) {
		case X86_G3:
			if (want_g3_exit) {
				want_g3_exit = 0;
				state = X86_G3S5;
				break;
			}

			in_want = 0;
			if (extpower_is_present())
				task_wait_event(-1);
			else {
				uint64_t target_time = last_shutdown_time +
						hibernate_delay * 1000000ull;
				time_now = get_time().val;
				if (time_now > target_time) {
					/*
					 * Time's up.  Hibernate until wake pin
					 * asserted.
					 */
					CPRINTF("[%T x86 hibernating]\n");
					system_hibernate(0, 0);
				} else {
					uint64_t wait = target_time - time_now;
					if (wait > TASK_MAX_WAIT_US)
						wait = TASK_MAX_WAIT_US;

					/* Wait for a message */
					task_wait_event(wait);
				}
			}
			break;

		case X86_S5:
			if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 1) {
				/* Power up to next state */
				state = X86_S5S3;
				break;
			}

			/* Wait for inactivity timeout */
			in_want = 0;
			if (task_wait_event(S5_INACTIVITY_TIMEOUT) ==
			    TASK_EVENT_TIMER) {
				/* Drop to G3; wake not requested yet */
				want_g3_exit = 0;
				state = X86_S5G3;
			}
			break;

		case X86_S3:
			/* Check for state transitions */
			if (!have_all_in_signals(IN_PGOOD_S3)) {
				/* Required rail went away */
				chipset_force_shutdown();
				state = X86_S3S5;
				break;
			} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1) {
				/* Power up to next state */
				state = X86_S3S0;
				break;
			} else if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 0) {
				/* Power down to next state */
				state = X86_S3S5;
				break;
			}

			/* Otherwise, steady state; wait for a message */
			in_want = 0;
			task_wait_event(-1);
			break;

		case X86_S0:
			if (!have_all_in_signals(IN_PGOOD_S0)) {
				/* Required rail went away */
				chipset_force_shutdown();
				state = X86_S0S3;
				break;
			} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
				/* Power down to next state */
				state = X86_S0S3;
				break;
			}

			/* Otherwise, steady state; wait for a message */
			in_want = 0;
			task_wait_event(-1);
			break;

		case X86_G3S5:
			/*
			 * Wait 10ms after +3VALW good, since that powers
			 * VccDSW and VccSUS.
			 */
			msleep(10);

			/* Assert DPWROK */
			gpio_set_level(GPIO_PCH_DPWROK, 1);
			if (wait_in_signals(IN_PCH_SLP_SUSn_DEASSERTED)) {
				chipset_force_shutdown();
				state = X86_G3;
				break;
			}

			gpio_set_level(GPIO_SUSP_VR_EN, 1);
			if (wait_in_signals(IN_PGOOD_PP1050)) {
				chipset_force_shutdown();
				state = X86_G3;
				break;
			}

			/* Deassert RSMRST# */
			gpio_set_level(GPIO_PCH_RSMRST_L, 1);

			/* Wait 5ms for SUSCLK to stabilize */
			msleep(5);

			state = X86_S5;
			break;

		case X86_S5S3:
			/* Enable PP5000 (5V) rail. */
			gpio_set_level(GPIO_PP5000_EN, 1);
			if (wait_in_signals(IN_PGOOD_PP5000)) {
				chipset_force_shutdown();
				state = X86_G3;
				break;
			}

			/* Wait for the always-on rails to be good */
			if (wait_in_signals(IN_PGOOD_ALWAYS_ON)) {
				chipset_force_shutdown();
				state = X86_S5;
			}

			/* Turn on power to RAM */
			gpio_set_level(GPIO_PP1350_EN, 1);
			if (wait_in_signals(IN_PGOOD_S3)) {
				chipset_force_shutdown();
				state = X86_S5;
			}

			/*
			 * Enable touchpad power so it can wake the system from
			 * suspend.
			 */
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);

			/* Call hooks now that rails are up */
			hook_notify(HOOK_CHIPSET_STARTUP);

			state = X86_S3;
			break;

		case X86_S3S0:
			/* Turn on power rails */
			gpio_set_level(GPIO_PP3300_DX_EN, 1);

			/* Enable WLAN */
			gpio_set_level(GPIO_PP3300_WLAN_EN, 1);
			gpio_set_level(GPIO_WLAN_OFF_L, 1);

			/* Wait for non-core power rails good */
			if (wait_in_signals(IN_PGOOD_S0)) {
				chipset_force_shutdown();
				gpio_set_level(GPIO_WLAN_OFF_L, 0);
				gpio_set_level(GPIO_PP3300_WLAN_EN, 0);
				gpio_set_level(GPIO_EC_EDP_VDD_EN, 0);
				gpio_set_level(GPIO_PP3300_DX_EN, 0);
				state = X86_S3;
				break;
			}

			/*
			 * Enable +CPU_CORE.  The CPU itself will request
			 * the supplies when it's ready.
			 */
			gpio_set_level(GPIO_VCORE_EN, 1);

			/* Call hooks now that rails are up */
			hook_notify(HOOK_CHIPSET_RESUME);

			/* Wait 99ms after all voltages good */
			msleep(99);

			/*
			 * Throttle CPU if necessary.  This should only be
			 * asserted when +VCCP is powered (it is by now).
			 */
			gpio_set_level(GPIO_CPU_PROCHOT, throttle_cpu);

			/* Set PCH_PWROK */
			gpio_set_level(GPIO_PCH_PWROK, 1);
			gpio_set_level(GPIO_SYS_PWROK, 1);

			state = X86_S0;
			break;

		case X86_S0S3:
			/* Call hooks before we remove power rails */
			hook_notify(HOOK_CHIPSET_SUSPEND);

			/* Clear PCH_PWROK */
			gpio_set_level(GPIO_SYS_PWROK, 0);
			gpio_set_level(GPIO_PCH_PWROK, 0);

			/* Wait 40ns */
			udelay(1);

			/* Disable +CPU_CORE */
			gpio_set_level(GPIO_VCORE_EN, 0);

			/* Disable WLAN */
			gpio_set_level(GPIO_WLAN_OFF_L, 0);
			gpio_set_level(GPIO_PP3300_WLAN_EN, 0);

			/*
			 * Deassert prochot since CPU is off and we're about
			 * to drop +VCCP.
			 */
			gpio_set_level(GPIO_CPU_PROCHOT, 0);

			/* Turn off power rails */
			gpio_set_level(GPIO_EC_EDP_VDD_EN, 0);
			gpio_set_level(GPIO_PP3300_DX_EN, 0);

			state = X86_S3;
			break;

		case X86_S3S5:
			/* Call hooks before we remove power rails */
			hook_notify(HOOK_CHIPSET_SHUTDOWN);

			/* Disable touchpad power */
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);

			/* Turn off power to RAM */
			gpio_set_level(GPIO_PP1350_EN, 0);

			/* Disable PP5000 (5V) rail. */
			gpio_set_level(GPIO_PP5000_EN, 0);

			state = X86_S5;
			break;

		case X86_S5G3:
			/* Deassert DPWROK, assert RSMRST# */
			gpio_set_level(GPIO_PCH_DPWROK, 0);
			gpio_set_level(GPIO_PCH_RSMRST_L, 0);

			gpio_set_level(GPIO_SUSP_VR_EN, 0);

			/* Record the time we go into G3 */
			last_shutdown_time = get_time().val;

			state = X86_G3;
			break;
		}
	}
}

/*****************************************************************************/
/* Console commands */

static int command_powerinfo(int argc, char **argv)
{
	/*
	 * Print x86 power state in same format as state machine.  This is
	 * used by FAFT tests, so must match exactly.
	 */
	ccprintf("[%T x86 power state %d = %s, in 0x%04x]\n",
		 state, state_names[state], in_signals);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerinfo, command_powerinfo,
			NULL,
			"Show current x86 power state",
			NULL);

static int command_x86indebug(int argc, char **argv)
{
	char *e;

	/* If one arg, set the mask */
	if (argc == 2) {
		int m = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		in_debug = m;
	}

	/* Print the mask */
	ccprintf("x86 in:     0x%04x\n", in_signals);
	ccprintf("debug mask: 0x%04x\n", in_debug);
	return EC_SUCCESS;
};
DECLARE_CONSOLE_COMMAND(x86indebug, command_x86indebug,
			"[mask]",
			"Get/set x86 input debug mask",
			NULL);

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
	if (state == X86_G3 && !extpower_is_present()) {
		ccprintf("Time G3: %d s\n", time_g3);
		ccprintf("Time left: %d s\n", hibernate_delay - time_g3);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hibdelay, command_hibernation_delay,
			"[sec]",
			"Set the delay before going into hibernation",
			NULL);
