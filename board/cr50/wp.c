/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "case_closed_debug.h"
#include "console.h"
#include "crc8.h"
#include "extension.h"
#include "gpio.h"
#include "hooks.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "physical_presence.h"
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

/**
 * Return non-zero if battery is present
 */
int board_battery_is_present(void)
{
	/* Invert because battery-present signal is active low */
	return !gpio_get_level(GPIO_BATT_PRES_L);
}

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
		/* Use battery presence as the value for write protect. */
		wp_en = board_battery_is_present();
	}

	/* Disable writing to the long life register */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 0);

	/* Update the WP state. */
	set_wp_state(!!wp_en);
}

static int command_wp(int argc, char **argv)
{
	int val = 1;
	int forced = 1;

	if (argc > 1) {
		/* Make sure we're allowed to override WP settings */
		if (!ccd_is_cap_enabled(CCD_CAP_OVERRIDE_WP))
			return EC_ERROR_ACCESS_DENIED;

		/* Update WP */
		if (strncasecmp(argv[1], "follow_batt_pres", 16) == 0)
			forced = 0;
		else if (parse_bool(argv[1], &val))
			forced = 1;
		else
			return EC_ERROR_PARAM1;

		force_write_protect(forced, val);

		if (argc > 2 && !strcasecmp(argv[2], "atboot")) {
			/* Change override at boot to match */
			ccd_set_flag(CCD_FLAG_OVERRIDE_WP_AT_BOOT, forced);
			ccd_set_flag(CCD_FLAG_OVERRIDE_WP_STATE_ENABLED, val);
		}
	}

	/* Invert, because active low */
	val = !GREG32(RBOX, EC_WP_L);
	forced = GREG32(PMU, LONG_LIFE_SCRATCH1) & BOARD_FORCING_WP;
	ccprintf("Flash WP: %s%s\n", forced ? "forced " : "",
		 val ? "enabled" : "disabled");

	ccprintf(" at boot: ");
	if (ccd_get_flag(CCD_FLAG_OVERRIDE_WP_AT_BOOT))
		ccprintf("forced %s\n",
			 ccd_get_flag(CCD_FLAG_OVERRIDE_WP_STATE_ENABLED)
			 ? "enabled" : "disabled");
	else
		ccprintf("follow_batt_pres\n");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(wp, command_wp,
			     "[<BOOLEAN>/follow_batt_pres [atboot]]",
			     "Get/set the flash HW write-protect signal");

void init_wp_state(void)
{
	/* Check system reset flags after CCD config is initially loaded */
	if ((system_get_reset_flags() & RESET_FLAG_HIBERNATE) &&
	    !system_rollback_detected()) {
		/*
		 * Deep sleep resume without rollback, so reload the WP state
		 * that was saved to the long-life registers before the deep
		 * sleep instead of going back to the at-boot default.
		 */
		if (GREG32(PMU, LONG_LIFE_SCRATCH1) & BOARD_FORCING_WP) {
			/* Temporarily forcing WP */
			set_wp_state(GREG32(PMU, LONG_LIFE_SCRATCH1) &
				     BOARD_WP_ASSERTED);
		} else {
			/* Write protected if battery is present */
			set_wp_state(board_battery_is_present());
		}
	} else if (ccd_get_flag(CCD_FLAG_OVERRIDE_WP_AT_BOOT)) {
		/* Reset to at-boot state specified by CCD */
		force_write_protect(1, ccd_get_flag(
		    CCD_FLAG_OVERRIDE_WP_STATE_ENABLED));
	} else {
		/* Reset to WP based on battery-present (val is ignored) */
		force_write_protect(0, 1);
	}
}

/**
 * Wipe the TPM
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int board_wipe_tpm(void)
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

		/*
		 * That should never return, but if it did, pass through the
		 * error we got.
		 */
		return rc;
	}

	CPRINTS("TPM is erased");

	/* Tell the TPM task to re-enable NvMem commits. */
	tpm_reinstate_nvmem_commits();

	return EC_SUCCESS;
}

/****************************************************************************/
/* FWMP TPM NVRAM space support */

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

/**
 * Return non-zero if FWMP allows unlock
 */
int board_fwmp_allows_unlock(void)
{
	/*
	 * TODO(rspangler): This doesn't work right for CCD config unlock and
	 * open, because read_fwmp() isn't called until TPM2_Startup is sent by
	 * the AP.  But that means if the AP can't boot, it's not possible to
	 * unlock or open CCD.
	 *
	 * CCD config isn't connected to anything else yet, so let's bypass
	 * the fwmp check for now.  But we need to fix this before we make
	 * a Cr50 release that could run on a MP device.
	 */
#ifdef CR50_DEV
	return 1;
#else
	return fwmp_allows_unlock;
#endif
}

/****************************************************************************/
/* Console control */

int console_is_restricted(void)
{
	return !ccd_is_cap_enabled(CCD_CAP_CR50_RESTRICTED_CONSOLE);
}

/****************************************************************************/
/* Stuff for the unlock sequence */

/**
 * Enable/disable power button interrupt.
 *
 * @param enable	Enable (!=0) or disable (==0)
 */
static void power_button_enable_interrupt(int enable)
{
	if (enable) {
		/* Clear any leftover power button interrupts */
		GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);

		/* Enable power button interrupt */
		GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 1);
		task_enable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);
	} else {
		GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 0);
		task_disable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);
	}
}

static void power_button_handler(void)
{
	CPRINTS("power button pressed");

	if (physical_detect_press() != EC_SUCCESS) {
		/* Not consumed by physical detect */
#ifdef CONFIG_U2F
		/* Track last power button press for U2F */
		power_button_record();
#endif
	}

	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);
}
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT, power_button_handler, 1);

#ifdef CONFIG_U2F
static void power_button_init(void)
{
	/*
	 * Enable power button interrupts all the time for U2F.
	 *
	 * Ideally U2F should only enable physical presence after the start of
	 * a U2F request (using atomic operations for the PP enable mask so it
	 * plays nicely with CCD config), but that doesn't happen yet.
	 */
	power_button_enable_interrupt(1);
}
DECLARE_HOOK(HOOK_INIT, power_button_init, HOOK_PRIO_DEFAULT);
#endif  /* CONFIG_U2F */

void board_physical_presence_enable(int enable)
{
#ifndef CONFIG_U2F
	/* Enable/disable power button interrupts */
	power_button_enable_interrupt(enable);
#endif

	/* Stay awake while we're doing this, just in case. */
	if (enable)
		disable_sleep(SLEEP_MASK_PHYSICAL_PRESENCE);
	else
		enable_sleep(SLEEP_MASK_PHYSICAL_PRESENCE);
}

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

	/* I have no idea what you're talking about */
	*response_size = 0;
	return VENDOR_RC_NO_SUCH_COMMAND;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_GET_LOCK, vc_lock);

/*
 * TODO(rspangler): The old concept of 'lock the console' really meant
 * something closer to 'reset CCD config', not the CCD V1 meaning of 'ccdlock'.
 * This command is no longer supported, so will fail.  It was defined this
 * way:
 *
 * DECLARE_VENDOR_COMMAND(VENDOR_CC_SET_LOCK, vc_lock);
 */
