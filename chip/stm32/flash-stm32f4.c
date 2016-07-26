/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for stm32f4 */

#include "clock.h"
#include "compile_time_macros.h"
#include "console.h"
#include "common.h"
#include "flash.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "panic.h"
#include "watchdog.h"


#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)

/*
 * Approximate number of CPU cycles per iteration of the loop when polling
 * the flash status
 */
#define CYCLE_PER_FLASH_LOOP 10

/* Flash page programming timeout.  This is 2x the datasheet max. */
#define FLASH_TIMEOUT_US 16000

static inline int calculate_flash_timeout(void)
{
	return (FLASH_TIMEOUT_US *
		(clock_get_freq() / SECOND) / CYCLE_PER_FLASH_LOOP);
}


/* Flag indicating whether we have locked down entire flash */
static int entire_flash_locked;

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */
#define FLASH_HOOK_VERSION 1
/* The previous write protect state before sys jump */
struct flash_wp_state {
	int entire_flash_locked;
};

/*****************************************************************************/
/* Physical layer APIs */

/* Flash unlocking keys */
#define PRG_LOCK 0
#define KEY1    0x45670123
#define KEY2    0xCDEF89AB

static int unlock(void)
{
	/*
	 * We may have already locked the flash module and get a bus fault
	 * in the attempt to unlock. Need to disable bus fault handler now.
	 */
	ignore_bus_fault(1);

	/* unlock CR if needed */
	if (STM32_FLASH_CR & FLASH_CR_LOCK) {
		STM32_FLASH_KEYR = KEY1;
		STM32_FLASH_KEYR = KEY2;
	}

	/* Re-enable bus fault handler */
	ignore_bus_fault(0);

	return (STM32_FLASH_CR & FLASH_CR_LOCK) ?
			EC_ERROR_UNKNOWN : EC_SUCCESS;
}

static void lock(void)
{
	STM32_FLASH_CR = FLASH_CR_LOCK;
}


int flash_physical_get_protect(int block)
{
	/* TODO: not sure if write protect can be implemented like this. */
	return 0;
}

uint32_t flash_physical_get_protect_flags(void)
{
	return entire_flash_locked ? EC_FLASH_PROTECT_ALL_NOW : 0;
}

int flash_physical_protect_now(int all)
{
	if (all) {
		/*
		 * Lock by writing a wrong key to FLASH_KEYR. This triggers a
		 * bus fault, so we need to disable bus fault handler while
		 * doing this.
		 *
		 * This incorrect key fault causes the flash to become
		 * permanenlty locked until reset, a correct keyring write
		 * will not unlock it. In this way we can implement system
		 * write protect.
		 */
		ignore_bus_fault(1);
		STM32_FLASH_KEYR = 0xffffffff;
		ignore_bus_fault(0);

		entire_flash_locked = 1;

		/* Check if lock happened */
		if (STM32_FLASH_CR & FLASH_CR_LOCK)
			return EC_SUCCESS;
	}

	/* No way to protect just the RO flash until next boot */
	return EC_ERROR_INVAL;
}

uint32_t flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT |
	       EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * If entire flash isn't protected at this boot, it can be enabled if
	 * the WP GPIO is asserted.
	 */
	if (!(cur_flags & EC_FLASH_PROTECT_ALL_NOW) &&
	    (cur_flags & EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_NOW;

	return ret;
}

int flash_physical_restore_state(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	int version, size;
	const struct flash_wp_state *prev;

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
		return 1;
	}

	return 0;
}

static int flash_idle(void)
{
	timestamp_t deadline;

	deadline.val = get_time().val + FLASH_TIMEOUT_US;
	/* Wait for flash op to complete.
	 * This function is used for both reads and writes, so
	 * we need a long timeout, but a relatively short poll interval.
	 */
	while ((STM32_FLASH_SR & FLASH_SR_BUSY) &&
		(get_time().val < deadline.val)) {
		usleep(1);
	}

	if (STM32_FLASH_SR & FLASH_SR_BUSY)
		return EC_ERROR_TIMEOUT;

	return EC_SUCCESS;
}

static void clear_flash_errors(void)
{
	/* Clear previous error status */
	STM32_FLASH_SR = FLASH_SR_ERR_MASK;
}

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_protect_at_boot(enum flash_wp_range range)
{
	return EC_SUCCESS;
}

int flash_physical_write(int offset, int size, const char *data)
{
	uint32_t *address = (uint32_t *)(CONFIG_MAPPED_STORAGE_BASE + offset);
	int res = EC_SUCCESS;

	if (unlock() != EC_SUCCESS) {
		res = EC_ERROR_UNKNOWN;
		goto exit_wr;
	}

	/* Wait for busy to clear */
	res = flash_idle();
	if (res)
		goto exit_wr;
	clear_flash_errors();

	/* set PG bit */
	STM32_FLASH_CR &= ~FLASH_CR_PSIZE_MASK;
	STM32_FLASH_CR |= FLASH_CR_PSIZE(FLASH_CR_PSIZE_32);
	STM32_FLASH_CR |= FLASH_CR_PG;

	for (; size > 0; size -= sizeof(uint32_t)) {
		/*
		 * Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing with interrupt disabled.
		 */
		watchdog_reload();

		res = flash_idle();
		if (res)
			goto exit_wr;

		/* write the word */
		*address = data[0] + (data[1] << 8) +
			   (data[2] << 16) + (data[3] << 24);

		address++;
		data += sizeof(uint32_t);

		res = flash_idle();
		if (res)
			goto exit_wr;

		if (STM32_FLASH_SR & FLASH_SR_BUSY) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error.
		 */
		if (STM32_FLASH_SR & FLASH_SR_ERR_MASK) {
			res = EC_ERROR_UNKNOWN;
			goto exit_wr;
		}
	}

exit_wr:
	/* Disable PG bit */
	STM32_FLASH_CR &= ~FLASH_CR_PG;

	lock();

	return res;
}



/* "@Internal Flash  /0x08000000/04*016Kg,01*064Kg,03*128Kg" */
struct flash_sector {
	int base;
	int size;
};
static const struct flash_sector sectors[] = {
	{(0 * 1024), (16 * 1024)},
	{(16 * 1024), (16 * 1024)},
	{(32 * 1024), (16 * 1024)},
	{(48 * 1024), (16 * 1024)},
	{(64 * 1024), (64 * 1024)},
	{(128 * 1024), (128 * 1024)},
	{(256 * 1024), (128 * 1024)},
	{(384 * 1024), (128 * 1024)}
};
static const int num_sectors = ARRAY_SIZE(sectors);

int flash_physical_erase(int offset, int size)
{
	int res = EC_SUCCESS;
	int start_sector;
	int end_sector;

	/* Check that offset/size align with sectors. */
	for (start_sector = 0; start_sector < num_sectors; start_sector++)
		if (offset == sectors[start_sector].base)
			break;
	for (end_sector = start_sector; end_sector < num_sectors; end_sector++)
		if ((offset + size) ==
		    (sectors[end_sector].base + sectors[end_sector].size))
			break;

	/* We can only erase on sector boundaries. */
	if ((start_sector >= num_sectors) || (end_sector >= num_sectors))
		return EC_ERROR_PARAM1;

	if (unlock() != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	res = flash_idle();
	if (res)
		goto exit_er;

	clear_flash_errors();

	for (; start_sector <= end_sector; start_sector++) {
		/* Do nothing if already erased */
		if (flash_is_erased(sectors[start_sector].base,
				    sectors[start_sector].size))
			continue;

		res = flash_idle();
		if (res)
			goto exit_er;

		/* set Sector Erase bit and select sector */
		STM32_FLASH_CR = (STM32_FLASH_CR & ~FLASH_CR_SNB_MASK) |
				FLASH_CR_SER | FLASH_CR_SNB(start_sector);

		/* set STRT bit : start erase */
		STM32_FLASH_CR |= FLASH_CR_STRT;

		/*
		 * Reload the watchdog timer to avoid watchdog reset during a
		 * long erase operation.
		 */
		watchdog_reload();

		/* Wait for erase to complete, this will be awhile */
		res = flash_idle();
		if (res)
			goto exit_er;
		/*
		 * Check for error conditions - erase failed, voltage error,
		 * protection error
		 */
		if (STM32_FLASH_SR & FLASH_SR_ERR_MASK) {
			res = EC_ERROR_UNKNOWN;
			goto exit_er;
		}
	}

exit_er:
	/* reset PER bit */
	STM32_FLASH_CR &= ~FLASH_CR_SER;

	lock();

	return res;
}

/*****************************************************************************/
/* High-level APIs */

int flash_pre_init(void)
{
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

