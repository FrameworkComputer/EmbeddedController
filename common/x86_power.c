/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "board.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "power_button.h"
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
#define DEFAULT_TIMEOUT 1000000

/* Timeout for dropping back from S5 to G3 */
#define S5_INACTIVITY_TIMEOUT 10000000

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
#define IN_PGOOD_5VALW             0x0001
#define IN_PGOOD_1_5V_DDR          0x0002
#define IN_PGOOD_1_5V_PCH          0x0004
#define IN_PGOOD_1_8VS             0x0008
#define IN_PGOOD_VCCP              0x0010
#define IN_PGOOD_VCCSA             0x0020
#define IN_PGOOD_CPU_CORE          0x0040
#define IN_PGOOD_VGFX_CORE         0x0080
#define IN_PCH_SLP_S3n_DEASSERTED  0x0100
#define IN_PCH_SLP_S4n_DEASSERTED  0x0200
#define IN_PCH_SLP_S5n_DEASSERTED  0x0400
#define IN_PCH_SLP_An_DEASSERTED   0x0800
#define IN_PCH_SLP_SUSn_DEASSERTED 0x1000
#define IN_PCH_SLP_MEn_DEASSERTED  0x2000
#define IN_PCH_SUSWARNn_DEASSERTED 0x4000
/* All always-on supplies */
#define IN_PGOOD_ALWAYS_ON   (IN_PGOOD_5VALW)
/* All non-core power rails */
#define IN_PGOOD_ALL_NONCORE (IN_PGOOD_1_5V_DDR | IN_PGOOD_1_5V_PCH |	\
			      IN_PGOOD_1_8VS | IN_PGOOD_VCCP | IN_PGOOD_VCCSA)
/* All core power rails */
#define IN_PGOOD_ALL_CORE    (IN_PGOOD_CPU_CORE | IN_PGOOD_VGFX_CORE)
/* All PM_SLP signals from PCH deasserted */
#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3n_DEASSERTED |		\
				  IN_PCH_SLP_S4n_DEASSERTED |		\
				  IN_PCH_SLP_S5n_DEASSERTED |		\
				  IN_PCH_SLP_An_DEASSERTED)
/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE |		\
		   IN_PGOOD_CPU_CORE | IN_ALL_PM_SLP_DEASSERTED)

static enum x86_state state = X86_G3;  /* Current state */
static uint32_t in_signals;   /* Current input signal states (IN_PGOOD_*) */
static uint32_t in_want;      /* Input signal state we're waiting for */
static uint32_t in_debug;     /* Signal values which print debug output */
static int want_g3_exit;      /* Should we exit the G3 state? */
static int throttle_cpu;      /* Throttle CPU? */

/* When did we enter G3? */
static uint64_t last_shutdown_time;
/* Delay before go into hibernation in seconds*/
static uint32_t hibernate_delay = 86400; /* 24 Hrs */

/* Update input signal state */
static void update_in_signals(void)
{
	uint32_t inew = 0;
	int v;

	if (gpio_get_level(GPIO_PGOOD_5VALW))
		inew |= IN_PGOOD_5VALW;

	if (gpio_get_level(GPIO_PGOOD_1_5V_DDR))
		inew |= IN_PGOOD_1_5V_DDR;
	if (gpio_get_level(GPIO_PGOOD_1_5V_PCH))
		inew |= IN_PGOOD_1_5V_PCH;
	if (gpio_get_level(GPIO_PGOOD_1_8VS))
		inew |= IN_PGOOD_1_8VS;
	if (gpio_get_level(GPIO_PGOOD_VCCP))
		inew |= IN_PGOOD_VCCP;
	if (gpio_get_level(GPIO_PGOOD_VCCSA))
		inew |= IN_PGOOD_VCCSA;

	if (gpio_get_level(GPIO_PGOOD_CPU_CORE))
		inew |= IN_PGOOD_CPU_CORE;
	if (gpio_get_level(GPIO_PGOOD_VGFX_CORE))
		inew |= IN_PGOOD_VGFX_CORE;

	if (gpio_get_level(GPIO_PCH_SLP_An))
		inew |= IN_PCH_SLP_An_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_S3n))
		inew |= IN_PCH_SLP_S3n_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_S4n))
		inew |= IN_PCH_SLP_S4n_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_S5n))
		inew |= IN_PCH_SLP_S5n_DEASSERTED;

	if (gpio_get_level(GPIO_PCH_SLP_SUSn))
		inew |= IN_PCH_SLP_SUSn_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_ME_CSW_DEVn))
		inew |= IN_PCH_SLP_MEn_DEASSERTED;

	v = gpio_get_level(GPIO_PCH_SUSWARNn);
	if (v)
		inew |= IN_PCH_SUSWARNn_DEASSERTED;
	/* Copy SUSWARN# signal from PCH to SUSACK# */
	gpio_set_level(GPIO_PCH_SUSACKn, v);

	if ((in_signals & in_debug) != (inew & in_debug))
		CPRINTF("[%T x86 in 0x%04x]\n", inew);

	in_signals = inew;
}

/*
 * Wait for all the inputs in <want> to be present.  Returns EC_ERROR_TIMEOUT
 * if timeout before reaching the desired state.
 */
static int wait_in_signals(uint32_t want)
{
	in_want = want;

	while ((in_signals & in_want) != in_want) {
		if (task_wait_event(DEFAULT_TIMEOUT) == TASK_EVENT_TIMER) {
			update_in_signals();
			CPRINTF("[x86 power timeout on input; "
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

void x86_power_cpu_overheated(int too_hot)
{
	static int overheat_count;

	if (too_hot) {
		overheat_count++;
		if (overheat_count > 3) {
			CPRINTF("[%T overheated; shutting down]\n");
			x86_power_force_shutdown();
			host_set_single_event(EC_HOST_EVENT_THERMAL_SHUTDOWN);
		}
	} else {
		overheat_count = 0;
	}
}

void x86_power_force_shutdown(void)
{
	CPRINTF("[%T x86 power force shutdown]\n");

	/*
	 * Force x86 off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	gpio_set_level(GPIO_PCH_DPWROK, 0);
	gpio_set_level(GPIO_PCH_RSMRSTn, 0);
}

void x86_power_reset(int cold_reset)
{
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

		/* Pulse must be at least 16 PCI clocks long = 500 ns */
		gpio_set_level(GPIO_PCH_RCINn, 0);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCINn, 1);
	}
}

/*****************************************************************************/
/* Chipset interface */

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
		task_wake(TASK_ID_X86POWER);
}

void chipset_throttle_cpu(int throttle)
{
	throttle_cpu = throttle;

	/* Immediately set throttling if CPU is on */
	if (state == X86_S0)
		gpio_set_level(GPIO_CPU_PROCHOT, throttle);
}

/*****************************************************************************/
/* Hooks */

/* Hook notified when lid state changes. */
static int x86_lid_change(void)
{
	/* Wake up the task to update power state */
	task_wake(TASK_ID_X86POWER);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_LID_CHANGE, x86_lid_change, HOOK_PRIO_DEFAULT);

/* Hook notified when AC state changes. */
static int x86_power_ac_change(void)
{
	if (power_ac_present()) {
		CPRINTF("[%T x86 AC on]\n");
		/* TODO: (crosbug.com/p/9609) re-enable turbo? */
	} else {
		CPRINTF("[%T x86 AC off]\n");
		/* TODO: (crosbug.com/p/9609) disable turbo */

		if (state == X86_G3) {
			last_shutdown_time = get_time().val;
			task_wake(TASK_ID_X86POWER);
		}
	}

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_AC_CHANGE, x86_power_ac_change, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Interrupts */

void x86_power_interrupt(enum gpio_signal signal)
{
	/* Shadow signals and compare with our desired signal state. */
	update_in_signals();

	/* Wake up the task */
	task_wake(TASK_ID_X86POWER);
}

/*****************************************************************************/
/* Initialization */

static int x86_power_init(void)
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
			CPUTS("[x86 already in S0]\n");
			state = X86_S0;
		} else {
			/* Force all signals to their G3 states */
			CPUTS("[x86 forcing G3]\n");
			gpio_set_level(GPIO_PCH_PWROK, 0);
			gpio_set_level(GPIO_ENABLE_VCORE, 0);
			gpio_set_level(GPIO_ENABLE_VS, 0);
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);
			gpio_set_level(GPIO_TOUCHSCREEN_RESETn, 0);
			gpio_set_level(GPIO_ENABLE_1_5V_DDR, 0);
			gpio_set_level(GPIO_PCH_RSMRSTn, 0);
			gpio_set_level(GPIO_PCH_DPWROK, 0);
			gpio_set_level(GPIO_ENABLE_5VALW, 0);
		}
	}

	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_PCH_BKLTEN);
	gpio_enable_interrupt(GPIO_PCH_SLP_An);
	gpio_enable_interrupt(GPIO_PCH_SLP_ME_CSW_DEVn);
	gpio_enable_interrupt(GPIO_PCH_SLP_S3n);
	gpio_enable_interrupt(GPIO_PCH_SLP_S4n);
	gpio_enable_interrupt(GPIO_PCH_SLP_S5n);
	gpio_enable_interrupt(GPIO_PCH_SLP_SUSn);
	gpio_enable_interrupt(GPIO_PCH_SUSWARNn);
	gpio_enable_interrupt(GPIO_PGOOD_1_5V_DDR);
	gpio_enable_interrupt(GPIO_PGOOD_1_5V_PCH);
	gpio_enable_interrupt(GPIO_PGOOD_1_8VS);
	gpio_enable_interrupt(GPIO_PGOOD_5VALW);
	gpio_enable_interrupt(GPIO_PGOOD_CPU_CORE);
	gpio_enable_interrupt(GPIO_PGOOD_VCCP);
	gpio_enable_interrupt(GPIO_PGOOD_VCCSA);
	gpio_enable_interrupt(GPIO_PGOOD_VGFX_CORE);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, x86_power_init, HOOK_PRIO_INIT_CHIPSET);

/*****************************************************************************/
/* Task function */

void x86_power_task(void)
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
			if (power_ac_present())
				task_wait_event(-1);
			else {
				uint64_t target_time = last_shutdown_time +
						hibernate_delay * 1000000ull;
				time_now = get_time().val;
				if (time_now > target_time) {
					/* Time's up. Hibernate as long as
					 * possible. */
					system_hibernate(0xffffffff, 0);
				}
				else {
					/* Wait for a message */
					task_wait_event(target_time - time_now);
				}
			}

			break;

		case X86_S5:
			if (gpio_get_level(GPIO_PCH_SLP_S5n) == 1) {
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
			/*
			 * If lid is closed; hold touchscreen in reset to cut
			 * power usage.  If lid is open, take touchscreen out
			 * of reset so it can wake the processor.
			 */
			gpio_set_level(GPIO_TOUCHSCREEN_RESETn,
				       power_lid_open_debounced());

			/* Check for state transitions */
			if (gpio_get_level(GPIO_PCH_SLP_S3n) == 1) {
				/* Power up to next state */
				state = X86_S3S0;
				break;
			} else if (gpio_get_level(GPIO_PCH_SLP_S5n) == 0) {
				/* Power down to next state */
				state = X86_S3S5;
				break;
			}

			/* Otherwise, steady state; wait for a message */
			in_want = 0;
			task_wait_event(-1);
			break;

		case X86_S0:
			if (gpio_get_level(GPIO_PCH_SLP_S3n) == 0) {
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
			usleep(10000);

			/* Assert DPWROK, deassert RSMRST# */
			gpio_set_level(GPIO_PCH_DPWROK, 1);
			gpio_set_level(GPIO_PCH_RSMRSTn, 1);

			/* Wait 5ms for SUSCLK to stabilize */
			usleep(5000);

			state = X86_S5;
			break;

		case X86_S5S3:
			/* Switch on +5V always-on */
			gpio_set_level(GPIO_ENABLE_5VALW, 1);
			/* Wait for the always-on rails to be good */
			wait_in_signals(IN_PGOOD_ALWAYS_ON);

			/*
			 * Take lightbar out of reset, now that +5VALW is
			 * available and we won't leak +3VALW through the reset
			 * line.
			 */
			gpio_set_level(GPIO_LIGHTBAR_RESETn, 1);

			/* Turn on power to RAM */
			gpio_set_level(GPIO_ENABLE_1_5V_DDR, 1);

			/*
			 * Enable touchpad power so it can wake the system from
			 * suspend.
			 */
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);

			/* Call hooks now that rails are up */
			hook_notify(HOOK_CHIPSET_STARTUP, 0);

			state = X86_S3;
			break;

		case X86_S3S0:
			/* Turn on power rails */
			gpio_set_level(GPIO_ENABLE_VS, 1);

			/* Enable WLAN */
			gpio_set_level(GPIO_ENABLE_WLAN, 1);
			gpio_set_level(GPIO_RADIO_ENABLE_WLAN, 1);
			gpio_set_level(GPIO_RADIO_ENABLE_BT, 1);

			/*
			 * Make sure touchscreen is out if reset (even if the
			 * lid is still closed); it may have been turned off if
			 * the lid was closed in S3.
			 */
			gpio_set_level(GPIO_TOUCHSCREEN_RESETn, 1);

			/* Wait for non-core power rails good */
			wait_in_signals(IN_PGOOD_ALL_NONCORE);

			/*
			 * Enable +CPU_CORE and +VGFX_CORE regulator.  The CPU
			 * itself will request the supplies when it's ready.
			 */
			gpio_set_level(GPIO_ENABLE_VCORE, 1);

			/* Call hooks now that rails are up */
			hook_notify(HOOK_CHIPSET_RESUME, 0);

			/* Wait 99ms after all voltages good */
			usleep(99000);

			/*
			 * Throttle CPU if necessary.  This should only be
			 * asserted when +VCCP is powered (it is by now).
			 */
			gpio_set_level(GPIO_CPU_PROCHOT, throttle_cpu);

			/* Set PCH_PWROK */
			gpio_set_level(GPIO_PCH_PWROK, 1);

			state = X86_S0;
			break;

		case X86_S0S3:
			/* Call hooks before we remove power rails */
			hook_notify(HOOK_CHIPSET_SUSPEND, 0);

			/* Clear PCH_PWROK */
			gpio_set_level(GPIO_PCH_PWROK, 0);

			/* Wait 40ns */
			udelay(1);

			/* Disable +CPU_CORE and +VGFX_CORE */
			gpio_set_level(GPIO_ENABLE_VCORE, 0);

			/* Disable WLAN */
			gpio_set_level(GPIO_ENABLE_WLAN, 0);
			gpio_set_level(GPIO_RADIO_ENABLE_WLAN, 0);
			gpio_set_level(GPIO_RADIO_ENABLE_BT, 0);

			/*
			 * Deassert prochot since CPU is off and we're about
			 * to drop +VCCP.
			 */
			gpio_set_level(GPIO_CPU_PROCHOT, 0);

			/* Turn off power rails */
			gpio_set_level(GPIO_ENABLE_VS, 0);

			state = X86_S3;
			break;

		case X86_S3S5:
			/* Call hooks before we remove power rails */
			hook_notify(HOOK_CHIPSET_SHUTDOWN, 0);

			/* Disable touchpad power */
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);

			/* Turn off power to RAM */
			gpio_set_level(GPIO_ENABLE_1_5V_DDR, 0);

			/*
			 * Put touchscreen and lightbar in reset, so we won't
			 * leak +3VALW through the reset line.
			 */
			gpio_set_level(GPIO_TOUCHSCREEN_RESETn, 0);
			gpio_set_level(GPIO_LIGHTBAR_RESETn, 0);

			/* Switch off +5V always-on */
			gpio_set_level(GPIO_ENABLE_5VALW, 0);

			state = X86_S5;
			break;

		case X86_S5G3:
			/* Deassert DPWROK, assert RSMRST# */
			gpio_set_level(GPIO_PCH_DPWROK, 0);
			gpio_set_level(GPIO_PCH_RSMRSTn, 0);

			/* Record the time we go into G3 */
			last_shutdown_time = get_time().val;

			state = X86_G3;
			break;
		}
	}
}

/*****************************************************************************/
/* Console commands */

static int command_x86reset(int argc, char **argv)
{
	int is_cold = 1;

	if (argc > 1 && !strcasecmp(argv[1], "cold"))
		is_cold = 1;
	else if (argc > 1 && !strcasecmp(argv[1], "warm"))
		is_cold = 0;

	/* Force the x86 to reset */
	ccprintf("Issuing x86 %s reset...\n", is_cold ? "cold" : "warm");
	x86_power_reset(is_cold);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(x86reset, command_x86reset,
			"[warm | cold]",
			"Issue x86 reset",
			NULL);

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

static int command_x86shutdown(int argc, char **argv)
{
	x86_power_force_shutdown();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(x86shutdown, command_x86shutdown,
			NULL,
			"Force x86 shutdown",
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
				/ 1000000;

	if (argc >= 2) {
		uint32_t s = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		hibernate_delay = s;
	}

	/* Print the current setting */
	ccprintf("Hibernation delay: %d s\n", hibernate_delay);
	if (state == X86_G3 && !power_ac_present()) {
		ccprintf("Time G3: %d s\n", time_g3);
		ccprintf("Time left: %d s\n", hibernate_delay - time_g3);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hibdelay, command_hibernation_delay,
			"[sec]",
			"Set the delay before going into hibernation",
			NULL);

/*****************************************************************************/
/* Host commands */

/* TODO: belongs in power_button.c since it owns switches? */
static int switch_command_enable_wireless(struct host_cmd_handler_args *args)
{
	const struct ec_params_switch_enable_wireless *p = args->params;

	gpio_set_level(GPIO_RADIO_ENABLE_WLAN,
		       p->enabled & EC_WIRELESS_SWITCH_WLAN);
	gpio_set_level(GPIO_RADIO_ENABLE_BT,
		       p->enabled & EC_WIRELESS_SWITCH_BLUETOOTH);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_ENABLE_WIRELESS,
		     switch_command_enable_wireless,
		     EC_VER_MASK(0));
