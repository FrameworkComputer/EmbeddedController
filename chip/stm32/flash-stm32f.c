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
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/*
 * Approximate number of CPU cycles per iteration of the loop when polling
 * the flash status
 */
#define CYCLE_PER_FLASH_LOOP 10

/* Flash page programming timeout.  This is 2x the datasheet max. */
#define FLASH_TIMEOUT_US 16000
#define FLASH_TIMEOUT_LOOP \
	(FLASH_TIMEOUT_US * (CPU_CLOCK / SECOND) / CYCLE_PER_FLASH_LOOP)

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

/* Flag indicating whether we have locked down entire flash */
static int entire_flash_locked;

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */
#define FLASH_HOOK_VERSION 1
/* The previous write protect state before sys jump */
/*
 * TODO(crosbug.com/p/23798): check if STM32L code works here too - that is,
 * check if entire flash is locked by attempting to lock it rather than keeping
 * a global variable.
 */
struct flash_wp_state {
	int entire_flash_locked;
};

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
		return EC_SUCCESS;

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

	*hword = value;

	/* reset OPTPG bit */
	STM32_FLASH_CR &= ~OPTPG;

	rv = wait_busy();
	if (rv)
		return rv;
	lock();

	return EC_SUCCESS;
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

	for (; size > 0; size -= sizeof(uint16_t)) {
		/*
		 * Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing with interrupt disabled.
		 */
		watchdog_reload();

		/* wait to be ready  */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP);
		     i++)
			;

		/* write the half word */
		*address++ = data[0] + (data[1] << 8);
		data += 2;

		/* Wait for writes to complete */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP);
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

		/*
		 * Reload the watchdog timer to avoid watchdog reset during a
		 * long erase operation.
		 */
		watchdog_reload();

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

		/*
		 * Check for error conditions - erase failed, voltage error,
		 * protection error
		 */
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

uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	/* Read all-protected state from our shadow copy */
	if (entire_flash_locked)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	return flags;
}

int flash_physical_protect_ro_at_boot(int enable)
{
	int block;
	int i;
	int original_val[4], val[4];

	for (i = 0; i < 4; ++i)
		original_val[i] = val[i] = read_optb(i * 2 + 8);

	for (block = RO_BANK_OFFSET;
	     block < RO_BANK_OFFSET + RO_BANK_COUNT + PSTATE_BANK_COUNT;
	     block++) {
		int byte_off = STM32_OPTB_WRP_OFF(block/8) / 2 - 4;
		if (enable)
			val[byte_off] = val[byte_off] & (~(1 << (block % 8)));
		else
			val[byte_off] = val[byte_off] | (1 << (block % 8));
	}

	for (i = 0; i < 4; ++i)
		if (original_val[i] != val[i])
			write_optb(i * 2 + 8, val[i]);

	return EC_SUCCESS;
}

int flash_physical_protect_now(int all)
{
	if (all) {
		/*
		 * Lock by writing a wrong key to FLASH_KEYR. This triggers a
		 * bus fault, so we need to disable bus fault handler while
		 * doing this.
		 */
		ignore_bus_fault(1);
		STM32_FLASH_KEYR = 0xffffffff;
		ignore_bus_fault(0);

		entire_flash_locked = 1;

		return EC_SUCCESS;
	} else {
		/* No way to protect just the RO flash until next boot */
		return EC_ERROR_INVAL;
	}
}

/**
 * Check if write protect register state is inconsistent with RO_AT_BOOT and
 * ALL_AT_BOOT state.
 *
 * @return zero if consistent, non-zero if inconsistent.
 */
static int registers_need_reset(void)
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
			 * Pstate wants RO protected at boot, but the write
			 * protect register wasn't set to protect it.  Force an
			 * update to the write protect register and reboot so
			 * it takes effect.
			 */
			flash_physical_protect_ro_at_boot(1);
			need_reset = 1;
		}

		if (registers_need_reset()) {
			/*
			 * Write protect register was in an inconsistent state.
			 * Set it back to a good state and reboot.
			 *
			 * TODO(crosbug.com/p/23798): this seems really similar
			 * to the check above.  One of them should be able to
			 * go away.
			 */
			flash_protect_ro_at_boot(
				prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT);
			need_reset = 1;
		}
	} else {
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

/*****************************************************************************/
/* Hooks */

static void flash_preserve_state(void)
{
	struct flash_wp_state state;

	state.entire_flash_locked = entire_flash_locked;

	system_add_jump_tag(FLASH_SYSJUMP_TAG, FLASH_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, flash_preserve_state, HOOK_PRIO_DEFAULT);
