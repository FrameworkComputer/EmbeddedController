/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "crc8.h"
#include "extension.h"
#include "gpio.h"
#include "hooks.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "registers.h"
#include "scratch_reg1.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "timer.h"
#include "tpm_nvmem_read.h"
#include "tpm_registers.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_RBOX, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_RBOX, format, ## args)

void set_wp_state(int asserted)
{
	/* Enable writing to the long life register */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 1);

	if (asserted) {
		GREG32(PMU, LONG_LIFE_SCRATCH1) |= BOARD_WP_ASSERTED;
		GREG32(RBOX, EC_WP_L) = 0;
	} else {
		GREG32(PMU, LONG_LIFE_SCRATCH1) &= ~BOARD_WP_ASSERTED;
		GREG32(RBOX, EC_WP_L) = 1;
	}

	/* Disable writing to the long life register */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 0);
}

/**
 * Force write protect state or follow battery presence.
 *
 * @param force: Force write protect to wp_en if non-zero, otherwise use battery
 *               presence as the source.
 * @param wp_en: 0: Deassert WP. 1: Assert WP.
 */
static void force_write_protect(int force, int wp_en)
{
	/* Enable writing to the long life register */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 1);

	if (force) {
		/* Force WP regardless of battery presence. */
		GREG32(PMU, LONG_LIFE_SCRATCH1) |= BOARD_FORCING_WP;
	} else {
		/* Stop forcing write protect. */
		GREG32(PMU, LONG_LIFE_SCRATCH1) &= ~BOARD_FORCING_WP;
		/*
		 * Use battery presence as the value for write protect.
		 * Inverted because the signal is active low.
		 */
		wp_en = !gpio_get_level(GPIO_BATT_PRES_L);
	}

	/* Disable writing to the long life register */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 0);

	/* Update the WP state. */
	set_wp_state(!!wp_en);
}

static int command_wp(int argc, char **argv)
{
	int val;
	int forced;

	if (argc > 1) {
		if (console_is_restricted()) {
			ccprintf("Console is locked, no parameters allowed\n");
		} else {
			if (strncasecmp(argv[1], "follow_batt_pres", 16) == 0)
				force_write_protect(0, -1);
			else if (parse_bool(argv[1], &val))
				force_write_protect(1, val);
			else
				return EC_ERROR_PARAM1;
		}
	}

	/* Invert, because active low */
	val = !GREG32(RBOX, EC_WP_L);

	forced = GREG32(PMU, LONG_LIFE_SCRATCH1) & BOARD_FORCING_WP;
	ccprintf("Flash WP is %s%s\n", forced ? "forced " : "",
		 val ? "enabled" : "disabled");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(wp, command_wp,
			     "[<BOOLEAN>/follow_batt_pres]",
			     "Get/set the flash HW write-protect signal");

/* When the system is locked down, provide a means to unlock it */
#ifdef CONFIG_RESTRICTED_CONSOLE_COMMANDS

#define LOCK_ENABLED 1

/* Hand-built images may be initially unlocked; Buildbot images are not. */
#ifdef CR50_DEV
static int console_restricted_state = !LOCK_ENABLED;
#else
static int console_restricted_state = LOCK_ENABLED;
#endif

static void set_console_lock_state(int lock_state)
{
	uint8_t nv_console_lock_state;
	uint8_t key;
	const struct tuple *t;
	int rv;

	/*
	 * Assert WP unconditionally on locked console. Keep this invocation
	 * separate, as it will also enable/disable writes into
	 * LONG_LIFE_SCRATCH1
	 */
	if (lock_state == LOCK_ENABLED)
		set_wp_state(1);

	/* Retrieve the console locked state. */
	key = NVMEM_VAR_CONSOLE_LOCKED;
	t = getvar((const uint8_t *)&key, sizeof(key));
	if (t == NULL) {
		CPRINTS("Failed to read lock state from nvmem!");
		/*
		 * It's possible that the tuple doesn't (yet) exist.  Set the
		 * value to some unknown.
		 */
		nv_console_lock_state = '?';
	} else {
		nv_console_lock_state = *tuple_val(t);
	}

	/* Update the NVMem state if it differs. */
	if (lock_state != nv_console_lock_state) {
		uint8_t val = lock_state == LOCK_ENABLED;

		rv = setvar((const uint8_t *)&key, 1, (const uint8_t *)&val, 1);
		if (rv) {
			CPRINTS("Failed to save nvmem tuple in RAM buffer!"
				" (rv: %d)", rv);
			return;
		}

		rv = writevars();
		if (rv) {
			CPRINTS("Failed to save lock state in nvmem! (rv:%d)",
				rv);
			return;
		}
	}

	/* Update our RAM copy. */
	console_restricted_state = lock_state;

	CPRINTS("The console is %s",
		lock_state == LOCK_ENABLED ? "locked" : "unlocked");
}

static void lock_the_console(void)
{
	set_console_lock_state(LOCK_ENABLED);
}

static void unlock_the_console(void)
{
	int rc;

	/* Wipe the TPM's memory and reset the TPM task. */
	rc = tpm_reset_request(1, 1);
	if (rc != EC_SUCCESS) {
		/*
		 * If anything goes wrong (which is unlikely), we REALLY don't
		 * want to unlock the console. It's possible to fail without
		 * the TPM task ever running, so rebooting is probably our best
		 * bet for fixing the problem.
		 */
		CPRINTS("%s: Couldn't wipe nvmem! (rc %d)", __func__, rc);
		cflush();
		system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED |
			     SYSTEM_RESET_HARD);
	}

	CPRINTS("TPM is erased");

	/* Tell the TPM task to re-enable NvMem commits. */
	tpm_reinstate_nvmem_commits();

	/* Unlock the console. */
	set_console_lock_state(!LOCK_ENABLED);
}

static void init_console_lock_and_wp(void)
{
	uint8_t key;
	const struct tuple *t;
	uint8_t lock_state;
	uint32_t reset_flags;

	reset_flags = system_get_reset_flags();
	/*
	 * On an unexpected reboot or a system rollback reset the console lock
	 * and write protect states.
	 */
	if (system_rollback_detected() ||
	    !(reset_flags & (RESET_FLAG_HIBERNATE | RESET_FLAG_POWER_ON))) {
		/* Reset the console lock to the default value */
		CPRINTS("Setting console lock to default.");
		set_console_lock_state(console_restricted_state);

		/* Use BATT_PRES_L as the source for write protect. */
		set_wp_state(!gpio_get_level(GPIO_BATT_PRES_L));
		return;
	}

	key = NVMEM_VAR_CONSOLE_LOCKED;
	t = getvar((const uint8_t *)&key, 1);
	if (t == NULL) {
		/*
		 * If the tuple doesn't exist, just use the default value (which
		 * will also create the tuple).
		 */
		CPRINTS("No tuple in nvmem.  Setting console lock to default.");
		set_console_lock_state(console_restricted_state);
	} else {
		lock_state = *tuple_val(t);
		set_console_lock_state(lock_state);
	}

	if (reset_flags & RESET_FLAG_HIBERNATE) {
		if (GREG32(PMU, LONG_LIFE_SCRATCH1) & BOARD_WP_ASSERTED)
			set_wp_state(1);
		else
			set_wp_state(0);
	} else if (reset_flags & RESET_FLAG_POWER_ON) {
		/* Use BATT_PRES_L as the source for write protect. */
		set_wp_state(!gpio_get_level(GPIO_BATT_PRES_L));
	}
}
/* This must run after initializing the NVMem partitions. */
DECLARE_HOOK(HOOK_INIT, init_console_lock_and_wp, HOOK_PRIO_DEFAULT+1);

/*
 * These definitions and the structure layout were manually copied from
 * src/platform/vboot_reference/firmware/lib/include/rollback_index.h. at
 * git sha c7282f6.
 */
#define FWMP_NV_INDEX		    0x100a
#define FWMP_HASH_SIZE		    32
#define FWMP_DEV_DISABLE_CCD_UNLOCK (1 << 6)

/* Firmware management parameters */
struct RollbackSpaceFwmp {
	/* CRC-8 of fields following struct_size */
	uint8_t crc;
	/* Structure size in bytes */
	uint8_t struct_size;
	/* Structure version */
	uint8_t struct_version;
	/* Reserved; ignored by current reader */
	uint8_t reserved0;
	/* Flags; see enum fwmp_flags */
	uint32_t flags;
	/* Hash of developer kernel key */
	uint8_t dev_key_hash[FWMP_HASH_SIZE];
} __packed;

static int lock_enforced(const struct RollbackSpaceFwmp *fwmp)
{
	uint8_t crc;

	/* Let's verify that the FWMP structure makes sense. */
	if (fwmp->struct_size != sizeof(*fwmp)) {
		CPRINTS("%s: fwmp size mismatch (%d)\n", __func__,
			fwmp->struct_size);
		return 1;
	}

	crc = crc8(&fwmp->struct_version, sizeof(struct RollbackSpaceFwmp) -
		   offsetof(struct RollbackSpaceFwmp, struct_version));
	if (fwmp->crc != crc) {
		CPRINTS("%s: fwmp crc mismatch\n", __func__);
		return 1;
	}

	return !!(fwmp->flags & FWMP_DEV_DISABLE_CCD_UNLOCK);
}

static int fwmp_allows_unlock;
void read_fwmp(void)
{
	/* Let's see if FWMP disables console activation. */
	struct RollbackSpaceFwmp fwmp;

	switch (read_tpm_nvmem(FWMP_NV_INDEX,
			       sizeof(struct RollbackSpaceFwmp), &fwmp)) {
	default:
		/* Something is messed up, let's not allow console unlock. */
		fwmp_allows_unlock = 0;
		break;

	case tpm_read_not_found:
		fwmp_allows_unlock = 1;
		break;

	case tpm_read_success:
		fwmp_allows_unlock = !lock_enforced(&fwmp);
		break;
	}

	CPRINTS("Console unlock %sallowed", fwmp_allows_unlock ? "" : "not ");
}

int console_is_restricted(void)
{
#ifndef CR50_DEV
	return fwmp_allows_unlock ? console_restricted_state : 1;
#else
	return console_restricted_state;
#endif
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
		cflush();
		unlock_the_console();
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
}

static void power_button_handler(void)
{
	if (unlock_in_progress)
		power_button_poked();

	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);
}
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT, power_button_handler, 1);

static void start_unlock_process(int total_poking_time, int max_poke_interval)
{
	unlock_in_progress = 1;

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

static void power_button_init(void)
{
	/* Clear any leftover power button interrupts */
	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);

	/* Enable power button interrupt */
	GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 1);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);
}
DECLARE_HOOK(HOOK_INIT, power_button_init, HOOK_PRIO_DEFAULT);

/****************************************************************************/
/* TPM vendor-specific commands */

static enum vendor_cmd_rc vc_lock(enum vendor_cmd_cc code,
				  void *buf,
				  size_t input_size,
				  size_t *response_size)
{
	uint8_t *buffer = buf;

	if (code == VENDOR_CC_GET_LOCK) {
		/*
		 * Get the state of the console lock.
		 *
		 *   Args: none
		 *   Returns: one byte; true (locked) or false (unlocked)
		 */
		if (input_size != 0) {
			*response_size = 0;
			return VENDOR_RC_BOGUS_ARGS;
		}

		buffer[0] = console_is_restricted() ? 0x01 : 0x00;
		*response_size = 1;
		return VENDOR_RC_SUCCESS;
	}

	if (code == VENDOR_CC_SET_LOCK) {
		/*
		 * Lock the console if it isn't already. Note that there
		 * intentionally isn't an unlock command. At most, we may want
		 * to call start_unlock_process(), but we haven't yet decided.
		 *
		 *   Args: none
		 *   Returns: none
		 */
		if (input_size != 0) {
			*response_size = 0;
			return VENDOR_RC_BOGUS_ARGS;
		}

		lock_the_console();
		*response_size = 0;
		return VENDOR_RC_SUCCESS;
	}

	/* I have no idea what you're talking about */
	*response_size = 0;
	return VENDOR_RC_NO_SUCH_COMMAND;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_GET_LOCK, vc_lock);
DECLARE_VENDOR_COMMAND(VENDOR_CC_SET_LOCK, vc_lock);

/****************************************************************************/
static const char warning[] = "\n\t!!! WARNING !!!\n\n"
	"\tThe AP will be impolitely shut down and the TPM persistent memory\n"
	"\tERASED before the console is unlocked. The system will reboot in\n"
	"\tnormal mode and ALL encrypted content will be LOST.\n\n"
	"\tIf this is not what you want, simply do nothing and the unlock\n"
	"\tprocess will fail.\n\n"
	"\n\t!!! WARNING !!!\n\n";

static int command_lock(int argc, char **argv)
{
	int enable;
	int i;

	if (argc > 1) {
		if (!parse_bool(argv[1], &enable))
			return EC_ERROR_PARAM1;

		/* Changing nothing does nothing */
		if (enable == console_is_restricted())
			goto out;

		/* Locking the console is always allowed */
		if (enable)  {
			lock_the_console();
			goto out;
		}

		if (!fwmp_allows_unlock) {
#ifdef CR50_DEV
			ccprintf("Ignoring FWMP unlock setting\n");
#else
			ccprintf("Managed device console can't be unlocked\n");
			goto out;
#endif
		}

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
