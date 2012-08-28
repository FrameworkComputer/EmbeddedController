/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "registers.h"
#include "panic.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#define US_PER_SECOND 1000000

/* the approximate number of CPU cycles per iteration of the loop when polling
 * the flash status
 */
#define CYCLE_PER_FLASH_LOOP 10

/* Flash page programming timeout.  This is 2x the datasheet max. */
#define FLASH_TIMEOUT_US 16000
#define FLASH_TIMEOUT_LOOP \
	(FLASH_TIMEOUT_US * (CPU_CLOCK / US_PER_SECOND) / CYCLE_PER_FLASH_LOOP)

/* Flash unlocking keys */
#define KEY1    0x45670123
#define KEY2    0xCDEF89AB

/* Lock bits for FLASH_CR register */
#define PG       (1<<0)
#define PER      (1<<1)
#define OPTPG    (1<<4)
#define OPTER    (1<<5)
#define STRT     (1<<6)
#define CR_LOCK  (1<<7)
#define PRG_LOCK 0
#define OPT_LOCK (1<<9)

#define PHYSICAL_BANKS (CONFIG_FLASH_PHYSICAL_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Persistent protection state flash offset / size / bank */
#define PSTATE_OFFSET     CONFIG_SECTION_FLASH_PSTATE_OFF
#define PSTATE_SIZE       CONFIG_SECTION_FLASH_PSTATE_SIZE
#define PSTATE_BANK       (PSTATE_OFFSET / CONFIG_FLASH_BANK_SIZE)
#define PSTATE_BANK_COUNT (PSTATE_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Read-only firmware offset and size in units of flash banks */
#define RO_BANK_OFFSET (CONFIG_SECTION_RO_OFF / CONFIG_FLASH_BANK_SIZE)
#define RO_BANK_COUNT  (CONFIG_SECTION_RO_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Read-write firmware offset and size in units of flash banks */
#define RW_BANK_OFFSET (CONFIG_SECTION_RW_OFF / CONFIG_FLASH_BANK_SIZE)
#define RW_BANK_COUNT  (CONFIG_SECTION_RW_SIZE / CONFIG_FLASH_BANK_SIZE)

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

/* Flag indicating whether we have locked down entire flash */
static int entire_flash_locked;

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */
#define FLASH_HOOK_VERSION 1
/* The previous write protect state before sys jump */
struct flash_wp_state {
	int entire_flash_locked;
};

/* Functions defined in system.c to access backup registers */
int system_set_fake_wp(int val);
int system_get_fake_wp(void);

static int write_optb(int byte, uint8_t value);

static int wait_busy(void)
{
	int timeout = FLASH_TIMEOUT_LOOP;
	while (STM32_FLASH_SR & (1 << 0) && timeout-- > 0)
		udelay(CYCLE_PER_FLASH_LOOP);
	return (timeout > 0) ? EC_SUCCESS : EC_ERROR_TIMEOUT;
}


static int unlock(int locks)
{
	/*
	 * We may have already locked the flash module and get a bus fault
	 * in the attempt to unlock. Need to disable bus fault handler now.
	 */
	ignore_bus_fault(1);

	/* unlock CR if needed */
	if (STM32_FLASH_CR & CR_LOCK) {
		STM32_FLASH_KEYR = KEY1;
		STM32_FLASH_KEYR = KEY2;
	}
	/* unlock option memory if required */
	if ((locks & OPT_LOCK) && !(STM32_FLASH_CR & OPT_LOCK)) {
		STM32_FLASH_OPTKEYR = KEY1;
		STM32_FLASH_OPTKEYR = KEY2;
	}

	/* Re-enable bus fault handler */
	ignore_bus_fault(0);

	return ((STM32_FLASH_CR ^ OPT_LOCK) & (locks | CR_LOCK)) ?
			EC_ERROR_UNKNOWN : EC_SUCCESS;
}

static void lock(void)
{
	STM32_FLASH_CR = CR_LOCK;
}

/*
 * Option byte organization
 *
 *                 [31:24]    [23:16]    [15:8]   [7:0]
 *
 *   0x1FFF_F800    nUSER      USER       nRDP     RDP
 *
 *   0x1FFF_F804    nData1     Data1     nData0    Data0
 *
 *   0x1FFF_F808    nWRP1      WRP1      nWRP0     WRP0
 *
 *   0x1FFF_F80C    nWRP3      WRP2      nWRP2     WRP2
 *
 * Note that the variable with n prefix means the complement.
 */
static uint8_t read_optb(int byte)
{
	return *(uint8_t *)(STM32_OPTB_BASE + byte);
}

static int erase_optb(void)
{
	int rv;

	rv = wait_busy();
	if (rv)
		return rv;

	rv = unlock(OPT_LOCK);
	if (rv)
		return rv;

	/* Must be set in 2 separate lines. */
	STM32_FLASH_CR |= OPTER;
	STM32_FLASH_CR |= STRT;

	rv = wait_busy();
	if (rv)
		return rv;
	lock();

	return EC_SUCCESS;
}

/*
 * Since the option byte erase is WHOLE erase, this function is to keep
 * rest of bytes, but make this byte 0xff.
 * Note that this could make a recursive call to write_optb().
 */
static int preserve_optb(int byte)
{
	int i, rv;
	uint8_t optb[8];

	/* The byte has been reset, no need to run preserve. */
	if (*(uint16_t *)(STM32_OPTB_BASE + byte) == 0xffff)
		return EC_SUCCESS;;

	for (i = 0; i < ARRAY_SIZE(optb); ++i)
		optb[i] = read_optb(i * 2);

	optb[byte / 2] = 0xff;

	rv = erase_optb();
	if (rv)
		return rv;
	for (i = 0; i < ARRAY_SIZE(optb); ++i) {
		rv = write_optb(i * 2, optb[i]);
		if (rv)
			return rv;
	}

	return EC_SUCCESS;
}

static int write_optb(int byte, uint8_t value)
{
	volatile int16_t *hword = (uint16_t *)(STM32_OPTB_BASE + byte);
	int rv;

	rv = wait_busy();
	if (rv)
		return rv;

	/* The target byte is the value we want to write. */
	if (*(uint8_t *)hword == value)
		return EC_SUCCESS;

	/* Try to erase that byte back to 0xff. */
	rv = preserve_optb(byte);
	if (rv)
		return rv;

	/* The value is 0xff after erase. No need to write 0xff again. */
	if (value == 0xff)
		return EC_SUCCESS;

	rv = unlock(OPT_LOCK);
	if (rv)
		return rv;

	/* set OPTPG bit */
	STM32_FLASH_CR |= OPTPG;

	*hword = value ;

	/* reset OPTPG bit */
	STM32_FLASH_CR &= ~OPTPG;

	rv = wait_busy();
	if (rv)
		return rv;
	lock();

	return EC_SUCCESS;
}

/**
 * Read persistent state into pstate.
 */
static int read_pstate(struct persist_state *pstate)
{
	memcpy(pstate, flash_physical_dataptr(PSTATE_OFFSET), sizeof(*pstate));

	/* Sanity-check data and initialize if necessary */
	if (pstate->version != PERSIST_STATE_VERSION) {
		memset(pstate, 0, sizeof(*pstate));
		pstate->version = PERSIST_STATE_VERSION;
	}

	return EC_SUCCESS;
}

/**
 * Write persistent state from pstate, erasing if necessary.
 */
static int write_pstate(const struct persist_state *pstate)
{
	struct persist_state current_pstate;
	int rv;

	/* Check if pstate has actually changed */
	if (!read_pstate(&current_pstate) &&
	    !memcmp(&current_pstate, pstate, sizeof(*pstate)))
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

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_write(int offset, int size, const char *data)
{
	uint16_t *address = (uint16_t *)(CONFIG_FLASH_BASE + offset);
	int res = EC_SUCCESS;
	int i;

	if (unlock(PRG_LOCK) != EC_SUCCESS) {
		res = EC_ERROR_UNKNOWN;
		goto exit_wr;
	}

	/* Clear previous error status */
	STM32_FLASH_SR = 0x34;

	/* set PG bit */
	STM32_FLASH_CR |= PG;


	for ( ; size > 0; size -= sizeof(uint16_t)) {
#ifdef CONFIG_TASK_WATCHDOG
		/* Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing with interrupt disabled.
		 */
		watchdog_reload();
#endif
		/* wait to be ready  */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP) ;
		     i++)
			;

		/* write the half word */
		*address++ = data[0] + (data[1] << 8);
		data += 2;

		/* Wait for writes to complete */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP) ;
		     i++)
			;

		if (STM32_FLASH_SR & 1) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (STM32_FLASH_SR & 0x14) {
			res = EC_ERROR_UNKNOWN;
			goto exit_wr;
		}
	}

exit_wr:
	/* Disable PG bit */
	STM32_FLASH_CR &= ~PG;

	lock();

	return res;
}


int flash_physical_erase(int offset, int size)
{
	int res = EC_SUCCESS;

	if (unlock(PRG_LOCK) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/* Clear previous error status */
	STM32_FLASH_SR = 0x34;

	/* set PER bit */
	STM32_FLASH_CR |= PER;

	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE,
	     offset += CONFIG_FLASH_ERASE_SIZE) {
		timestamp_t deadline;

		/* Do nothing if already erased */
		if (flash_is_erased(offset, CONFIG_FLASH_ERASE_SIZE))
			continue;

		/* select page to erase */
		STM32_FLASH_AR = CONFIG_FLASH_BASE + offset;

		/* set STRT bit : start erase */
		STM32_FLASH_CR |= STRT;
#ifdef CONFIG_TASK_WATCHDOG
		/* Reload the watchdog timer in case the erase takes long time
		 * so that erasing many flash pages
		 */
		watchdog_reload();
#endif

		deadline.val = get_time().val + FLASH_TIMEOUT_US;
		/* Wait for erase to complete */
		while ((STM32_FLASH_SR & 1) &&
		       (get_time().val < deadline.val)) {
			usleep(300);
		}
		if (STM32_FLASH_SR & 1) {
			res = EC_ERROR_TIMEOUT;
			goto exit_er;
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (STM32_FLASH_SR & 0x14) {
			res = EC_ERROR_UNKNOWN;
			goto exit_er;
		}
	}

exit_er:
	/* reset PER bit */
	STM32_FLASH_CR &= ~PER;

	lock();

	return res;
}

int flash_physical_get_protect(int block)
{
	return entire_flash_locked || !(STM32_FLASH_WRPR & (1 << block));
}

static int flash_physical_get_protect_at_boot(int block)
{
	uint8_t val = read_optb(STM32_OPTB_WRP_OFF(block/8));
	return (!(val & (1 << (block % 8)))) ? 1 : 0;
}

static void flash_physical_set_protect_at_boot(int start_bank,
					       int bank_count,
					       int enable)
{
	int block;
	int i;
	int original_val[4], val[4];

	for (i = 0; i < 4; ++i)
		original_val[i] = val[i] = read_optb(i * 2 + 8);

	for (block = start_bank; block < start_bank + bank_count; block++) {
		int byte_off = STM32_OPTB_WRP_OFF(block/8) / 2 - 4;
		if (enable)
			val[byte_off] = val[byte_off] & (~(1 << (block % 8)));
		else
			val[byte_off] = val[byte_off] | (1 << (block % 8));
	}

	for (i = 0; i < 4; ++i)
		if (original_val[i] != val[i])
			write_optb(i * 2 + 8, val[i]);
}

static int protect_ro_at_boot(int enable, int force)
{
	struct persist_state pstate;
	int new_flags = enable ? PERSIST_FLAG_PROTECT_RO : 0;
	int rv;

	/* Read the current persist state from flash */
	rv = read_pstate(&pstate);
	if (rv)
		return rv;

	/* Update state if necessary */
	if (pstate.flags != new_flags || force) {
		/* Fail if write protect block is already locked */
		if (flash_physical_get_protect(PSTATE_BANK))
			return EC_ERROR_ACCESS_DENIED;

		/* Set the new flag */
		pstate.flags = new_flags;

		/* Write the state back to flash */
		rv = write_pstate(&pstate);
		if (rv)
			return rv;

		/*
		 * Write to write protect register.
		 * Since we already wrote to pstate, ignore error here.
		 */
		flash_physical_set_protect_at_boot(RO_BANK_OFFSET,
			RO_BANK_COUNT + PSTATE_BANK_COUNT, new_flags);
	}

	return EC_SUCCESS;
}

static int protect_entire_flash_until_reboot(void)
{
	/*
	 * Lock by writing a wrong key to FLASH_KEYR. This triggers a bus
	 * fault, so we need to disable bus fault handler while doing this.
	 */
	ignore_bus_fault(1);
	STM32_FLASH_KEYR = 0xffffffff;
	ignore_bus_fault(0);

	entire_flash_locked = 1;

	return EC_SUCCESS;
}

/**
 * Determine if write protect register is inconsistent with RO_AT_BOOT and
 * ALL_AT_BOOT state.
 */
static int register_need_reset(void)
{
	uint32_t flags = flash_get_protect();
	int i;
	int ro_at_boot = (flags & EC_FLASH_PROTECT_RO_AT_BOOT) ? 1 : 0;
	int ro_wp_region_start = RO_BANK_OFFSET;
	int ro_wp_region_end =
		RO_BANK_OFFSET + RO_BANK_COUNT + PSTATE_BANK_COUNT;

	for (i = ro_wp_region_start; i < ro_wp_region_end; i++)
		if (flash_physical_get_protect_at_boot(i) != ro_at_boot)
			return 1;
	return 0;
}

static void unprotect_all_blocks(void)
{
	int i;
	for (i = 4; i < 8; ++i)
		write_optb(i * 2, 0xff);
}

/*****************************************************************************/
/* High-level APIs */

int flash_pre_init(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	uint32_t prot_flags = flash_get_protect();
	int need_reset = 0;
	const struct flash_wp_state *prev;
	int version, size;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection. Nothing additional needs to be done.
	 */
	if (reset_flags & RESET_FLAG_SYSJUMP) {
		prev = (const struct flash_wp_state *)system_get_jump_tag(
				FLASH_SYSJUMP_TAG, &version, &size);
		if (prev && version == FLASH_HOOK_VERSION &&
		    size == sizeof(*prev))
			entire_flash_locked = prev->entire_flash_locked;
		return EC_SUCCESS;
	}

	if (prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
		if ((prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT) &&
		    !(prot_flags & EC_FLASH_PROTECT_RO_NOW)) {
			/*
			 * Pstate say "ro_at_boot". WP register says otherwise.
			 * Listen to pstate.
			 */
			protect_ro_at_boot(1, 1);
			need_reset = 1;
		}

		if (register_need_reset()) {
			/*
			 * Reset RO protect register to make sure this doesn't
			 * happen again due to RO protect state inconsistency.
			 */
			protect_ro_at_boot(
				prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT, 1);
			need_reset = 1;
		}
	}
	else {
		if (prot_flags & EC_FLASH_PROTECT_RO_NOW) {
			/*
			 * Write protect pin unasserted but some section is
			 * protected. Drop it and reboot.
			 */
			unprotect_all_blocks();
			need_reset = 1;
		}
	}

	if (need_reset)
		system_reset(SYSTEM_RESET_HARD | SYSTEM_RESET_PRESERVE_FLAGS);

	return EC_SUCCESS;
}

uint32_t flash_get_protect(void)
{
	struct persist_state pstate;
	uint32_t flags = 0;
	int i;
	int not_protected[2] = {0};

	if (system_get_fake_wp() || !gpio_get_level(GPIO_WRITE_PROTECTn))
		flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;

	/* Read the current persist state from flash */
	read_pstate(&pstate);
	if (pstate.flags & PERSIST_FLAG_PROTECT_RO)
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;

	if (entire_flash_locked) {
		flags |= EC_FLASH_PROTECT_ALL_NOW;
	}

	/* Scan flash protection */
	for (i = 0; i < PHYSICAL_BANKS; i++) {
		/* Is this bank part of RO? */
		int is_ro = (i >= RO_BANK_OFFSET &&
			     i < RO_BANK_OFFSET + RO_BANK_COUNT +
			     PSTATE_BANK_COUNT) ? 1 : 0;
		int bank_flag = (is_ro ? EC_FLASH_PROTECT_RO_NOW :
				 EC_FLASH_PROTECT_ALL_NOW);

		if (flash_physical_get_protect(i)) {
			flags |= bank_flag;
			if (not_protected[is_ro])
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		}
		else {
			not_protected[is_ro] = 1;
			if (flags & bank_flag)
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		}
	}

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
		rv = protect_ro_at_boot(flags & EC_FLASH_PROTECT_RO_AT_BOOT, 0);
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

	if (mask & flags & EC_FLASH_PROTECT_ALL_NOW) {
		/*
		 * Since RO is already protected, protecting entire flash
		 * is effectively protecting RW.
		 */
		rv = protect_entire_flash_until_reboot();
		if (rv)
			retval = rv;
	}

	return retval;
}

/* TODO: crosbug.com/p/12036 */
static int command_set_fake_wp(int argc, char **argv)
{
	int val;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	val = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	system_set_fake_wp(val);
	ccprintf("Fake write protect = %d\n", val);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fakewp, command_set_fake_wp,
			"<0 | 1>",
			"Set fake write protect pin",
			NULL);

/*****************************************************************************/
/* Hooks */

static int flash_preserve_state(void)
{
	struct flash_wp_state state;

	state.entire_flash_locked = entire_flash_locked;

	system_add_jump_tag(FLASH_SYSJUMP_TAG, FLASH_HOOK_VERSION,
			    sizeof(state), &state);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_SYSJUMP, flash_preserve_state, HOOK_PRIO_DEFAULT);
