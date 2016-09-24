/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "nvmem.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_RBOX, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_RBOX, format, ## args)

static int command_wp(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;

		/* Invert, because active low */
		GREG32(RBOX, EC_WP_L) = !val;
	}

	/* Invert, because active low */
	val = !GREG32(RBOX, EC_WP_L);

	ccprintf("Flash WP is %s\n", val ? "enabled" : "disabled");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(wp, command_wp,
			"[<BOOLEAN>]",
			"Get/set the flash HW write-protect signal");

/* When the system is locked down, provide a means to unlock it */
#ifdef CONFIG_RESTRICTED_CONSOLE_COMMANDS

/* Hand-built images may be initially unlocked; Buildbot images are not. */
#ifdef CR50_DEV
static int console_restricted_state;
#else
static int console_restricted_state = 1;
#endif

int console_is_restricted(void)
{
	return console_restricted_state;
}

/****************************************************************************/
/* Stuff for the unlock sequence */

/*
 * The normal unlock sequence should take 5 minutes (unless the case is
 * opened). Hand-built images only need to be long enough to demonstrate that
 * they work.
 */
#ifdef CR50_DEV
#define UNLOCK_SEQUENCE_DURATION (10 * SECOND)
#else
#define UNLOCK_SEQUENCE_DURATION (300 * SECOND)
#endif

/* Max time that can elapse between power button pokes */
static int unlock_beat;

/* When will we have poked the power button for long enough? */
static timestamp_t unlock_deadline;

/* Are we expecting power button pokes? */
static int unlock_in_progress;

/* This is invoked only when the unlock sequence has ended */
static void unlock_sequence_is_over(void)
{
	/* Disable the power button interrupt so we aren't bothered */
	GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 0);
	task_disable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);

	if (unlock_in_progress) {
		/* We didn't poke the button fast enough */
		CPRINTS("Unlock process failed");
	} else {
		/* The last poke was after the final deadline, so we're done */
		CPRINTS("Unlock process completed successfully");
		nvmem_wipe_or_reboot();
		console_restricted_state = 0;
		CPRINTS("TPM is erased, console is unlocked.");
	}

	unlock_in_progress = 0;

	/* Allow sleeping again */
	enable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
}
DECLARE_DEFERRED(unlock_sequence_is_over);

static void power_button_poked(void)
{
	if (timestamp_expired(unlock_deadline, NULL)) {
		/* We've been poking for long enough */
		unlock_in_progress = 0;
		hook_call_deferred(&unlock_sequence_is_over_data, 0);
		CPRINTS("poke: enough already", __func__);
	} else {
		/* Wait for the next poke */
		hook_call_deferred(&unlock_sequence_is_over_data, unlock_beat);
		CPRINTS("poke: not yet %.6ld", unlock_deadline);
	}

	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);
}
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT, power_button_poked, 1);


static void start_unlock_process(int total_poking_time, int max_poke_interval)
{
	unlock_in_progress = 1;

	/* Clear any leftover power button interrupts */
	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);

	/* Enable power button interrupt */
	GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 1);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);

	/* Must poke at least this often */
	unlock_beat = max_poke_interval;

	/* Keep poking until it's been long enough */
	unlock_deadline = get_time();
	unlock_deadline.val += total_poking_time;

	/* Stay awake while we're doing this, just in case. */
	disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);

	/* Check progress after waiting long enough for one button press */
	hook_call_deferred(&unlock_sequence_is_over_data, unlock_beat);
}

/****************************************************************************/
static const char warning[] = "\n\t!!! WARNING !!!\n\n"
	"\tThe AP will be impolitely shut down and the TPM persistent memory\n"
	"\tERASED before the console is unlocked. If this is not what you\n"
	"\twant, simply do nothing and the unlock process will fail.\n\n";

static int command_lock(int argc, char **argv)
{
	int enabled;
	int i;

	if (argc > 1) {
		if (!parse_bool(argv[1], &enabled))
			return EC_ERROR_PARAM1;

		/* Changing nothing does nothing */
		if (enabled == console_restricted_state)
			goto out;

		/* Locking the console is always allowed */
		if (enabled)  {
			console_restricted_state = 1;
			goto out;
		}

		/*
		 * TODO(crosbug.com/p/55322, crosbug.com/p/55728): There may be
		 * other preconditions which must be satisified before
		 * continuing. We can return EC_ERROR_ACCESS_DENIED if those
		 * aren't met.
		 */

		/* Don't count down if we know it's likely to fail */
		if (unlock_in_progress) {
			ccprintf("An unlock process is already in progress\n");
			return EC_ERROR_BUSY;
		}

		/* Warn about the side effects of wiping nvmem */
		ccputs(warning);

		if (gpio_get_level(GPIO_BATT_PRES_L) == 1) {
			/*
			 * If the battery cable has been disconnected, we only
			 * need to poke the power button once to prove physical
			 * presence.
			 */
			ccprintf("Tap the power button once to confirm...\n\n");

			/*
			 * We'll be satisified with the first press (so the
			 * unlock_deadine is now + 0us), but we're willing to
			 * wait for up to 10 seconds for that first press to
			 * happen. If we don't get one by then, the unlock will
			 * fail.
			 */
			start_unlock_process(0, 10 * SECOND);

		} else {
			/*
			 * If the battery is present, the user has to sit there
			 * and poke the button repeatedly until enough time has
			 * elapsed.
			 */

			ccprintf("Start poking the power button in ");
			for (i = 10; i; i--) {
				ccprintf("%d ", i);
				sleep(1);
			}
			ccprintf("go!\n");

			/*
			 * We won't be happy until we've been poking the button
			 * for a good long while, but we'll only wait a couple
			 * of seconds between each press before deciding that
			 * the user has given up.
			 */
			start_unlock_process(UNLOCK_SEQUENCE_DURATION,
					     2 * SECOND);

			ccprintf("Unlock sequence starting."
				 " Continue until %.6ld\n", unlock_deadline);
		}

		return EC_SUCCESS;
	}

out:
	ccprintf("The restricted console lock is %s\n",
		 console_is_restricted() ? "enabled" : "disabled");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(lock, command_lock,
			     "[<BOOLEAN>]",
			     "Get/Set the restricted console lock");

#endif	/* CONFIG_RESTRICTED_CONSOLE_COMMANDS */
