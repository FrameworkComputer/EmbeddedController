/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Skylake IMVP8 / ROP PMIC chipset power control module for Chrome EC */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_PCH_SLP_S0_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S0_DEASSERTED)
#define IN_PCH_SLP_S3_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)
#define IN_PCH_SLP_SUS_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)

#ifdef CONFIG_POWER_S0IX
#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S0_DEASSERTED | \
				  IN_PCH_SLP_S3_DEASSERTED | \
				  IN_PCH_SLP_S4_DEASSERTED | \
				  IN_PCH_SLP_SUS_DEASSERTED)
#else
#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3_DEASSERTED | \
				  IN_PCH_SLP_S4_DEASSERTED | \
				  IN_PCH_SLP_SUS_DEASSERTED)
#endif

/*
 * DPWROK is NC / stuffing option on initial boards.
 * TODO(shawnn): Figure out proper control signals.
 */
#define IN_PGOOD_ALL_CORE 0

#define IN_ALL_S0 (IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

static int throttle_cpu;      /* Throttle CPU? */
static int forcing_shutdown;  /* Forced shutdown in progress? */
static int power_s5_up;       /* Chipset is sequencing up or down */

void chipset_force_shutdown(void)
{
	CPRINTS("%s()", __func__);

	/*
	 * Force off. Sending a reset command to the PMIC will power off
	 * the EC, so simulate a long power button press instead. This
	 * condition will reset once the state machine transitions to G3.
	 * Consider reducing the latency here by changing the power off
	 * hold time on the PMIC.
	 */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		forcing_shutdown = 1;
		power_button_pch_press();
	}
}

static void chipset_force_g3(void)
{
	CPRINTS("Forcing fake G3.");

	gpio_set_level(GPIO_PMIC_SLP_SUS_L, 0);
}

void chipset_reset(int cold_reset)
{
	CPRINTS("%s(%d)", __func__, cold_reset);

	if (cold_reset) {
		if (gpio_get_level(GPIO_SYS_RESET_L) == 0)
			return;
		gpio_set_level(GPIO_SYS_RESET_L, 0);
		/* Debounce time for SYS_RESET_L is 16 ms */
		udelay(20 * MSEC);
		gpio_set_level(GPIO_SYS_RESET_L, 1);
	} else {
		/*
		 * Send a RCIN_PCH_RCIN_L
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system.
		 */

		/* Pulse must be at least 16 PCI clocks long = 500 ns */
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
	}
}

void chipset_throttle_cpu(int throttle)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_set_level(GPIO_CPU_PROCHOT, throttle);
}

enum power_state power_chipset_init(void)
{
	/*
	 * If we're switching between images without rebooting, see if the x86
	 * is already powered on; if so, leave it there instead of cycling
	 * through G3.
	 */
	if (system_jumped_to_this_image()) {
		if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			/* Disable idle task deep sleep when in S0. */
			disable_sleep(SLEEP_MASK_AP_RUN);
			CPRINTS("already in S0");
			return POWER_S0;
		} else {
			/* Force all signals to their G3 states */
			chipset_force_g3();
		}
	}

	return POWER_G3;
}

static void handle_rsmrst(enum power_state state)
{
	/*
	 * Pass through RSMRST asynchronously, as PCH may not react
	 * immediately to power changes.
	 */
	int rsmrst_in = gpio_get_level(GPIO_RSMRST_L_PGOOD);
	int rsmrst_out = gpio_get_level(GPIO_PCH_RSMRST_L);

	/* Nothing to do. */
	if (rsmrst_in == rsmrst_out)
		return;
	/*
	 * Wait at least 10ms between power signals going high
	 * and deasserting RSMRST to PCH.
	 */
	if (rsmrst_in)
		msleep(10);
	gpio_set_level(GPIO_PCH_RSMRST_L, rsmrst_in);
	CPRINTS("RSMRST: %d", rsmrst_in);
}

static void handle_slp_sus(enum power_state state)
{
	/* If we're down or going down don't do anythin with SLP_SUS_L. */
	if (state == POWER_G3 || state == POWER_S5G3)
		return;

	/* Always mimic PCH SLP_SUS request for all other states. */
	gpio_set_level(GPIO_PMIC_SLP_SUS_L, gpio_get_level(GPIO_PCH_SLP_SUS_L));
}

#ifdef CONFIG_BOARD_HAS_RTC_RESET
static enum power_state power_wait_s5_rtc_reset(void)
{
	static int s5_exit_tries;

	/* Wait for S5 exit and then attempt RTC reset */
	while ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == 0) {
		/* Handle RSMRST passthru event while waiting */
		handle_rsmrst(POWER_S5);
		if (task_wait_event(SECOND*4) == TASK_EVENT_TIMER) {
			CPRINTS("timeout waiting for S5 exit");
			chipset_force_g3();

			/* Assert RTCRST# and retry 5 times */
			board_rtc_reset();

			if (++s5_exit_tries > 4) {
				s5_exit_tries = 0;
				return POWER_G3; /* Stay off */
			}

			udelay(10 * MSEC);
			return POWER_G3S5; /* Power up again */
		}
	}

	s5_exit_tries = 0;
	return POWER_S5S3; /* Power up to next state */
}
#endif

static enum power_state _power_handle_state(enum power_state state)
{
	int tries = 0;

	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:
		if (forcing_shutdown) {
			power_button_pch_release();
			forcing_shutdown = 0;
		}

#ifdef CONFIG_BOARD_HAS_RTC_RESET
		/* Wait for S5 exit and attempt RTC reset it supported */
		if (power_s5_up)
			return power_wait_s5_rtc_reset();
#endif
		if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 1)
			return POWER_S5S3; /* Power up to next state */
		break;

	case POWER_S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S3S5;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}
		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			chipset_force_shutdown();
			return POWER_S0S3;
#ifdef CONFIG_POWER_S0IX
		} else if ((gpio_get_level(GPIO_PCH_SLP_S0_L) == 0) &&
			   (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1)) {
			return POWER_S0S0ix;
#endif
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
		}

		break;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		/*
		 * TODO: add code for unexpected power loss
		 */
		if ((gpio_get_level(GPIO_PCH_SLP_S0_L) == 1) &&
		   (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1)) {
			return POWER_S0ixS0;
		}

		break;
#endif

	case POWER_G3S5:
		/* Call hooks to initialize PMIC */
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		/*
		 * Allow up to 1s for charger to be initialized, in case
		 * we're trying to boot the AP with no battery.
		 */
		while (charge_prevent_power_on(0) &&
		       tries++ < CHARGER_INITIALIZED_TRIES) {
			msleep(CHARGER_INITIALIZED_DELAY_MS);
		}

		/* Return to G3 if battery level is too low */
		if (charge_want_shutdown() ||
		    tries > CHARGER_INITIALIZED_TRIES) {
			CPRINTS("power-up inhibited");
			chipset_force_shutdown();
			return POWER_G3;
		}

		if (power_wait_signals(IN_PCH_SLP_SUS_DEASSERTED)) {
			chipset_force_shutdown();
			return POWER_G3;
		}

		power_s5_up = 1;
		return POWER_S5;

	case POWER_S5S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S5G3;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S3S5;
		}

		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);

		/* Enable wireless */
		wireless_set_state(WIRELESS_ON);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * Throttle CPU if necessary.  This should only be asserted
		 * when +VCCP is powered (it is by now).
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, throttle_cpu);

		return POWER_S0;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);

		/* Suspend wireless */
		wireless_set_state(WIRELESS_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		return POWER_S3;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0S0ix:
		/* call hooks before standby */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S0ix.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		return POWER_S0ix;


	case POWER_S0ixS0:
		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		return POWER_S0;
#endif

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable wireless */
		wireless_set_state(WIRELESS_OFF);

		/* Always enter into S5 state. The S5 state is required to
		 * correctly handle global resets which have a bit of delay
		 * while the SLP_Sx_L signals are asserted then deasserted. */
		power_s5_up = 0;
		return POWER_S5;

	case POWER_S5G3:
		chipset_force_g3();
		return POWER_G3;

	default:
		break;
	}

	return state;
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state new_state;

	/* Process RSMRST_L state changes. */
	handle_rsmrst(state);

	new_state = _power_handle_state(state);

	/* Process SLP_SUS_L state changes after a new state is decided. */
	handle_slp_sus(new_state);

	return new_state;
}

#ifdef CONFIG_LOW_POWER_PSEUDO_G3
void enter_pseudo_g3(void)
{
	CPRINTS("Enter Psuedo G3");

	/*
	 * Clean up the UART buffer and prevent any unwanted garbage characters
	 * before power off and also ensure above debug message is printed.
	 */
	cflush();

	gpio_set_level(GPIO_G3_SLEEP_EN, 1);

	/* Power to EC should shut down now */
	while (1)
		;
}
#endif

#ifdef CONFIG_POWER_S0IX
static struct {
	int required; /* indicates de-bounce required. */
	int done;     /* debounced */
} slp_s0_debounce = {
	.required = 0,
	.done = 1,
};

int chipset_get_ps_debounced_level(enum gpio_signal signal)
{
	/*
	 * If power state is updated in power_update_signal() by any interrupts
	 * other than SLP_S0 during the 1 msec pulse(invalid SLP_S0 signal),
	 * reading SLP_S0 should be corrected with slp_s0_debounce.done flag.
	 */
	int level = gpio_get_level(signal);
	return (signal == GPIO_PCH_SLP_S0_L) ?
			(level & slp_s0_debounce.done) : level;
}

static void slp_s0_assertion_deferred(void)
{
	int s0_level = gpio_get_level(GPIO_PCH_SLP_S0_L);
	/*
	     (s0_level != 0) ||
	     ((s0_level == 0) && (slp_s0_debounce.required == 0))
	*/
	if (s0_level == slp_s0_debounce.required) {
		if (s0_level)
			slp_s0_debounce.done = 1; /* debounced! */

		power_signal_interrupt(GPIO_PCH_SLP_S0_L);
	}

	slp_s0_debounce.required = 0;
}
DECLARE_DEFERRED(slp_s0_assertion_deferred);

void power_signal_interrupt_S0(enum gpio_signal signal)
{
	if (gpio_get_level(GPIO_PCH_SLP_S0_L)) {
		slp_s0_debounce.required = 1;
		hook_call_deferred(slp_s0_assertion_deferred, 3 * MSEC);
	}
	else if (slp_s0_debounce.required == 0) {
		slp_s0_debounce.done = 0;
		slp_s0_assertion_deferred();
	}
}
#endif
