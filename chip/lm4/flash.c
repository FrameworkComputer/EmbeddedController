/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "flash.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#define FLASH_FWB_WORDS 32
#define FLASH_FWB_BYTES (FLASH_FWB_WORDS * 4)

#define BANK_SHIFT 5 /* bank registers have 32bits each, 2^32 */
#define BANK_MASK ((1 << BANK_SHIFT) - 1) /* 5 bits */
#define F_BANK(b) ((b) >> BANK_SHIFT)
#define F_BIT(b) (1 << ((b) & BANK_MASK))

/* Flash timeouts.  These are 2x the spec sheet max. */
#define ERASE_TIMEOUT_MS 200
#define WRITE_TIMEOUT_US 300

/* Number of physical banks of flash */
#define PHYSICAL_BANKS (CONFIG_FLASH_PHYSICAL_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Persistent protection state flash offset / size / bank */
#define PSTATE_OFFSET CONFIG_SECTION_FLASH_PSTATE_OFF
#define PSTATE_SIZE   CONFIG_SECTION_FLASH_PSTATE_SIZE
#define PSTATE_BANK   (PSTATE_OFFSET / CONFIG_FLASH_BANK_SIZE)

/* Read-only firmware offset and size in units of flash banks */
#define RO_BANK_OFFSET (CONFIG_SECTION_RO_OFF / CONFIG_FLASH_BANK_SIZE)
#define RO_BANK_COUNT  (CONFIG_SECTION_RO_SIZE / CONFIG_FLASH_BANK_SIZE)

int stuck_locked;  /* Is physical flash stuck protected? */
int all_protected; /* Has all-flash protection been requested? */

/* Persistent protection state - emulates a SPI status register for flashrom */
struct persist_state {
	uint8_t version;            /* Version of this struct */
	uint8_t flags;              /* Lock flags (PERSIST_FLAG_*) */
	uint8_t reserved[2];        /* Reserved; set 0 */
};

#define PERSIST_STATE_VERSION 2  /* Expected persist_state.version */

/* Flags for persist_state.flags */
/* Protect persist state and RO firmware at boot */
#define PERSIST_FLAG_PROTECT_RO 0x02

/**
 * Read persistent state into pstate.
 *
 * @param pstate	Destination for persistent state
 */
static void read_pstate(struct persist_state *pstate)
{
	memcpy(pstate, flash_physical_dataptr(PSTATE_OFFSET), sizeof(*pstate));

	/* Sanity-check data and initialize if necessary */
	if (pstate->version != PERSIST_STATE_VERSION) {
		memset(pstate, 0, sizeof(*pstate));
		pstate->version = PERSIST_STATE_VERSION;
	}
}

/**
 * Write persistent state from pstate, erasing if necessary.
 *
 * @param pstate	Source persistent state
 * @return EC_SUCCESS, or nonzero if error.
 */
static int write_pstate(const struct persist_state *pstate)
{
	struct persist_state current_pstate;
	int rv;

	/* Check if pstate has actually changed */
	read_pstate(&current_pstate);
	if (!memcmp(&current_pstate, pstate, sizeof(*pstate)))
		return EC_SUCCESS;

	/* Erase pstate */
	rv = flash_physical_erase(PSTATE_OFFSET, PSTATE_SIZE);
	if (rv)
		return rv;

	/*
	 * Note that if we lose power in here, we'll lose the pstate contents.
	 * That's ok, because it's only possible to write the pstate before
	 * it's protected.
	 */

	/* Rewrite the data */
	return flash_physical_write(PSTATE_OFFSET, sizeof(*pstate),
				    (const char *)pstate);
}

/**
 * Enable write protect for the read-only code.
 *
 * Once write protect is enabled, it will STAY enabled until the system is
 * hard-rebooted with the hardware write protect pin deasserted.  If the write
 * protect pin is deasserted, the protect setting is ignored, and the entire
 * flash will be writable.
 *
 * @param enable        Enable write protection
 * @return EC_SUCCESS, or nonzero if error.
 */
static int protect_ro_at_boot(int enable)
{
	struct persist_state pstate;
	int new_flags = enable ? PERSIST_FLAG_PROTECT_RO : 0;
	int rv;

	/* Read the current persist state from flash */
	read_pstate(&pstate);

	/* Update state if necessary */
	if (pstate.flags != new_flags) {

		/* Fail if write protect block is already locked */
		if (flash_physical_get_protect(PSTATE_BANK))
			return EC_ERROR_ACCESS_DENIED;

		/* Set the new flag */
		pstate.flags = new_flags;

		/* Write the state back to flash */
		rv = write_pstate(&pstate);
		if (rv)
			return rv;
	}

	return EC_SUCCESS;
}

/**
 * Protect flash banks until reboot.
 *
 * @param start_bank    Start bank to protect
 * @param bank_count    Number of banks to protect
 */
static void protect_banks(int start_bank, int bank_count)
{
	int bank;
	for (bank = start_bank; bank < start_bank + bank_count; bank++)
		LM4_FLASH_FMPPE[F_BANK(bank)] &= ~F_BIT(bank);
}

/**
 * Perform a write-buffer operation.  Buffer (FWB) and address (FMA) must be
 * pre-loaded.
 *
 * @return EC_SUCCESS, or nonzero if error.
 */
static int write_buffer(void)
{
	int t;

#ifdef BOARD_slippy			       /* FIXME: crosbug.com/p/19366 */
	return EC_ERROR_UNKNOWN;
#endif

	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	if (!LM4_FLASH_FWBVAL)
		return EC_SUCCESS;  /* Nothing to do */

	/* Clear previous error status */
	LM4_FLASH_FCMISC = LM4_FLASH_FCRIS;

	/* Start write operation at page boundary */
	LM4_FLASH_FMC2 = 0xa4420001;

#ifdef CONFIG_WATCHDOG
	/* Reload the watchdog timer, so that writing a large amount of flash
	 * doesn't cause a watchdog reset. */
	watchdog_reload();
#endif

	/* Wait for write to complete */
	for (t = 0; LM4_FLASH_FMC2 & 0x01; t += 10) {
		if (t > WRITE_TIMEOUT_US)
			return EC_ERROR_TIMEOUT;
		udelay(10);
	}

	/* Check for error conditions - program failed, erase needed,
	 * voltage error. */
	if (LM4_FLASH_FCRIS & 0x2e01)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_write(int offset, int size, const char *data)
{
	const uint32_t *data32 = (const uint32_t *)data;
	int rv;
	int i;

	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* Get initial write buffer index and page */
	LM4_FLASH_FMA = offset & ~(FLASH_FWB_BYTES - 1);
	i = (offset >> 2) & (FLASH_FWB_WORDS - 1);

	/* Copy words into buffer */
	for ( ; size > 0; size -= 4) {
		LM4_FLASH_FWB[i++] = *data32++;
		if (i == FLASH_FWB_WORDS) {
			rv = write_buffer();
			if (rv != EC_SUCCESS)
				return rv;

			/* Advance to next page */
			i = 0;
			LM4_FLASH_FMA += FLASH_FWB_BYTES;
		}
	}

	/* Handle final partial page, if any */
	if (i > 0)
		return write_buffer();

	return EC_SUCCESS;
}

int flash_physical_erase(int offset, int size)
{
#ifdef BOARD_slippy			       /* FIXME: crosbug.com/p/19366 */
	return EC_ERROR_UNKNOWN;
#endif
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	LM4_FLASH_FCMISC = LM4_FLASH_FCRIS;  /* Clear previous error status */

	for ( ; size > 0; size -= CONFIG_FLASH_ERASE_SIZE,
			offset += CONFIG_FLASH_ERASE_SIZE) {
		int t;

		/* Do nothing if already erased */
		if (flash_is_erased(offset, CONFIG_FLASH_ERASE_SIZE))
			continue;

		LM4_FLASH_FMA = offset;

#ifdef CONFIG_WATCHDOG
		/* Reload the watchdog timer, so that erasing many flash pages
		 * doesn't cause a watchdog reset.  May not need this now that
		 * we're using msleep() below. */
		watchdog_reload();
#endif

		/* Start erase */
		LM4_FLASH_FMC = 0xa4420002;

		/* Wait for erase to complete */
		for (t = 0; LM4_FLASH_FMC & 0x02; t++) {
			if (t > ERASE_TIMEOUT_MS)
				return EC_ERROR_TIMEOUT;
			msleep(1);
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (LM4_FLASH_FCRIS & 0x0a01)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

int flash_physical_get_protect(int bank)
{
	return (LM4_FLASH_FMPPE[F_BANK(bank)] & F_BIT(bank)) ? 0 : 1;
}

/*****************************************************************************/
/* High-level APIs */

uint32_t flash_get_protect(void)
{
	struct persist_state pstate;
	uint32_t flags = 0;
	int not_protected[2] = {0};
	int i;

	/* Read all-protected state from our shadow copy */
	if (all_protected)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	/* Read the current persist state from flash */
	read_pstate(&pstate);
	if (pstate.flags & PERSIST_FLAG_PROTECT_RO)
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;

#ifdef HAS_TASK_SWITCH
	/* Check if write protect pin is asserted now */
	if (switch_get_write_protect())
		flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;
#endif

	/* Scan flash protection */
	for (i = 0; i < PHYSICAL_BANKS; i++) {
		/* Is this bank part of RO? */
		int is_ro = ((i >= RO_BANK_OFFSET &&
			      i < RO_BANK_OFFSET + RO_BANK_COUNT) ||
			     i == PSTATE_BANK);
		int bank_flag = (is_ro ? EC_FLASH_PROTECT_RO_NOW :
				 EC_FLASH_PROTECT_ALL_NOW);

		if (flash_physical_get_protect(i)) {
			/* At least one bank in the region is protected */
			flags |= bank_flag;
			if (not_protected[is_ro])
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		} else {
			/* But not all banks in the region! */
			not_protected[is_ro] = 1;
			if (flags & bank_flag)
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		}
	}

	if ((flags & EC_FLASH_PROTECT_ALL_NOW) &&
	    !(flags & EC_FLASH_PROTECT_RO_NOW))
		flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	/* Check if blocks were stuck locked at pre-init */
	if (stuck_locked)
		flags |= EC_FLASH_PROTECT_ERROR_STUCK;

	return flags;
}

int flash_set_protect(uint32_t mask, uint32_t flags)
{
	int retval = EC_SUCCESS;
	int rv;

	/*
	 * Process flags we can set.  Track the most recent error, but process
	 * all flags before returning.
	 */
	if (mask & EC_FLASH_PROTECT_RO_AT_BOOT) {
		rv = protect_ro_at_boot(flags & EC_FLASH_PROTECT_RO_AT_BOOT);
		if (rv)
			retval = rv;
	}

	/*
	 * All subsequent flags only work if write protect is enabled (that is,
	 * hardware WP flag) *and* RO is protected at boot (software WP flag).
	 */
	if ((~flash_get_protect()) & (EC_FLASH_PROTECT_GPIO_ASSERTED |
				      EC_FLASH_PROTECT_RO_AT_BOOT))
		return retval;

	if ((mask & EC_FLASH_PROTECT_RO_NOW) &&
	    (flags & EC_FLASH_PROTECT_RO_NOW)) {
		/* Protect pstate */
		protect_banks(PSTATE_BANK, 1);

		/* Protect the read-only section */
		protect_banks(RO_BANK_OFFSET, RO_BANK_COUNT);
	}

	if ((mask & EC_FLASH_PROTECT_ALL_NOW) &&
	    (flags & EC_FLASH_PROTECT_ALL_NOW)) {
		/* Protect the entire flash */
		all_protected = 1;
		protect_banks(0, CONFIG_FLASH_PHYSICAL_SIZE /
			      CONFIG_FLASH_BANK_SIZE);
	}

	return retval;
}

int flash_pre_init(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	uint32_t prot_flags = flash_get_protect();
	uint32_t unwanted_prot_flags = EC_FLASH_PROTECT_ALL_NOW |
		EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection.  Nothing additional needs to be done.
	 */
	if (reset_flags & RESET_FLAG_SYSJUMP)
		return EC_SUCCESS;

	if ((prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED)) {
		/*
		 * Write protect is asserted.  If we want RO flash protected,
		 * protect it now.
		 */
		if ((prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT) &&
		    !(prot_flags & EC_FLASH_PROTECT_RO_NOW)) {
			int rv = flash_set_protect(EC_FLASH_PROTECT_RO_NOW,
						   EC_FLASH_PROTECT_RO_NOW);
			if (rv)
				return rv;

			/* Re-read flags */
			prot_flags = flash_get_protect();
		}

		/* Update all-now flag if all flash is protected */
		if (prot_flags & EC_FLASH_PROTECT_ALL_NOW)
			all_protected = 1;

	} else {
		/* Don't want RO flash protected */
		unwanted_prot_flags |= EC_FLASH_PROTECT_RO_NOW;
	}

	/* If there are no unwanted flags, done */
	if (!(prot_flags & unwanted_prot_flags))
		return EC_SUCCESS;

	/*
	 * If the last reboot was a power-on reset, it should have cleared
	 * write-protect.  If it didn't, then the flash write protect registers
	 * have been permanently committed and we can't fix that.
	 */
	if (reset_flags & RESET_FLAG_POWER_ON) {
		stuck_locked = 1;
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Otherwise, do a hard boot to clear the flash protection registers */
	system_reset(SYSTEM_RESET_HARD | SYSTEM_RESET_PRESERVE_FLAGS);

	/* That doesn't return, so if we're still here that's an error */
	return EC_ERROR_UNKNOWN;
}
