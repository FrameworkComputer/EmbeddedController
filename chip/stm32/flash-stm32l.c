/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "clock.h"
#include "console.h"
#include "flash.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/*
 * Approximate number of CPU cycles per iteration of the loop when polling
 * the flash status.
 */
#define CYCLE_PER_FLASH_LOOP 10

/* Flash page programming timeout.  This is 2x the datasheet max. */
#define FLASH_TIMEOUT_MS 16

static int flash_timeout_loop;

/**
 * Lock all the locks.
 */
static void lock(void)
{
	ignore_bus_fault(1);

	STM32_FLASH_PECR = STM32_FLASH_PECR_PE_LOCK |
		STM32_FLASH_PECR_PRG_LOCK | STM32_FLASH_PECR_OPT_LOCK;

	ignore_bus_fault(0);
}

/**
 * Unlock the specified locks.
 */
static int unlock(int locks)
{
	/*
	 * We may have already locked the flash module and get a bus fault
	 * in the attempt to unlock. Need to disable bus fault handler now.
	 */
	ignore_bus_fault(1);

	/* Unlock PECR if needed */
	if (STM32_FLASH_PECR & STM32_FLASH_PECR_PE_LOCK) {
		STM32_FLASH_PEKEYR = STM32_FLASH_PEKEYR_KEY1;
		STM32_FLASH_PEKEYR = STM32_FLASH_PEKEYR_KEY2;
	}

	/* Fail if it didn't unlock */
	if (STM32_FLASH_PECR & STM32_FLASH_PECR_PE_LOCK) {
		ignore_bus_fault(0);
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Unlock program memory if required */
	if ((locks & STM32_FLASH_PECR_PRG_LOCK) &&
	    (STM32_FLASH_PECR & STM32_FLASH_PECR_PRG_LOCK)) {
		STM32_FLASH_PRGKEYR = STM32_FLASH_PRGKEYR_KEY1;
		STM32_FLASH_PRGKEYR = STM32_FLASH_PRGKEYR_KEY2;
	}

	/* Unlock option memory if required */
	if ((locks & STM32_FLASH_PECR_OPT_LOCK) &&
	    (STM32_FLASH_PECR & STM32_FLASH_PECR_OPT_LOCK)) {
		STM32_FLASH_OPTKEYR = STM32_FLASH_OPTKEYR_KEY1;
		STM32_FLASH_OPTKEYR = STM32_FLASH_OPTKEYR_KEY2;
	}

	ignore_bus_fault(0);

	/* Successful if we unlocked everything we wanted */
	if (!(STM32_FLASH_PECR & (locks | STM32_FLASH_PECR_PE_LOCK)))
		return EC_SUCCESS;

	/* Otherwise relock everything and return error */
	lock();
	return EC_ERROR_ACCESS_DENIED;
}

/**
 * Read an option byte word.
 *
 * Option bytes are stored in pairs in 32-bit registers; the upper 16 bits is
 * the 1's compliment of the lower 16 bits.
 */
static uint16_t read_optb(int offset)
{
	return REG16(STM32_OPTB_BASE + offset);
}

/**
 * Write an option byte word.
 *
 * Requires OPT_LOCK unlocked.
 */
static void write_optb(int offset, uint16_t value)
{
	REG32(STM32_OPTB_BASE + offset) =
		(uint32_t)value | ((uint32_t)(~value) << 16);
}

/**
 * Read the at-boot protection option bits.
 */
static uint32_t read_optb_wrp(void)
{
	return read_optb(STM32_OPTB_WRP1L) |
		((uint32_t)read_optb(STM32_OPTB_WRP1H) << 16);
}

/**
 * Write the at-boot protection option bits.
 */
static void write_optb_wrp(uint32_t value)
{
	write_optb(STM32_OPTB_WRP1L, (uint16_t)value);
	write_optb(STM32_OPTB_WRP1H, value >> 16);
}

/**
 * Write data to flash.
 *
 * This function lives in internal RAM, as we cannot read flash during writing.
 * You must not call other functions from this one or declare it static.
 */
void  __attribute__((section(".iram.text")))
	iram_flash_write(uint32_t *addr, uint32_t *data)
{
	int i;

	/* Wait for ready  */
	for (i = 0; (STM32_FLASH_SR & 1) && (i < flash_timeout_loop) ;
	     i++)
		;

	/* Set PROG and FPRG bits */
	STM32_FLASH_PECR |= STM32_FLASH_PECR_PROG | STM32_FLASH_PECR_FPRG;

	/* Send words for the half page */
	for (i = 0; i < CONFIG_FLASH_WRITE_SIZE / sizeof(uint32_t); i++)
		*addr++ = *data++;

	/* Wait for writes to complete */
	for (i = 0; ((STM32_FLASH_SR & 9) != 8) && (i < flash_timeout_loop) ;
	     i++)
		;

	/* Disable PROG and FPRG bits */
	STM32_FLASH_PECR &= ~(STM32_FLASH_PECR_PROG | STM32_FLASH_PECR_FPRG);
}

int flash_physical_write(int offset, int size, const char *data)
{
	uint32_t *data32 = (uint32_t *)data;
	uint32_t *address = (uint32_t *)(CONFIG_FLASH_BASE + offset);
	int res = EC_SUCCESS;
	int word_mode = 0;
	int i;

	/* Fail if offset, size, and data aren't at least word-aligned */
	if ((offset | size | (uint32_t)(uintptr_t)data) & 3)
		return EC_ERROR_INVAL;

	/* Unlock program area */
	res = unlock(STM32_FLASH_PECR_PRG_LOCK);
	if (res)
		goto exit_wr;

	/* Clear previous error status */
	STM32_FLASH_SR = 0xf00;

	/*
	 * If offset and size aren't on word boundaries, do word writes.  This
	 * is slower, but since we claim to the outside world that writes must
	 * be half-page size, the only code which hits this path is writing
	 * pstate (which is just writing one word).
	 */
	if ((offset | size) & (CONFIG_FLASH_WRITE_SIZE - 1))
		word_mode = 1;

	/* Update flash timeout based on current clock speed */
	flash_timeout_loop = FLASH_TIMEOUT_MS * (clock_get_freq() / MSEC) /
		CYCLE_PER_FLASH_LOOP;

	while (size > 0) {
		/*
		 * Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing with interrupt disabled.
		 */
		watchdog_reload();

		if (word_mode) {
			/* Word write */
			*address++ = *data32++;

			/* Wait for writes to complete */
			for (i = 0; ((STM32_FLASH_SR & 9) != 8) &&
				     (i < flash_timeout_loop) ; i++)
				;

			size -= sizeof(uint32_t);
		} else {
			/* Half page write */
			interrupt_disable();
			iram_flash_write(address, data32);
			interrupt_enable();
			address += CONFIG_FLASH_WRITE_SIZE / sizeof(uint32_t);
			data32 += CONFIG_FLASH_WRITE_SIZE / sizeof(uint32_t);
			size -= CONFIG_FLASH_WRITE_SIZE;
		}

		if (STM32_FLASH_SR & 1) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		/*
		 * Check for error conditions: erase failed, voltage error,
		 * protection error
		 */
		if (STM32_FLASH_SR & 0xf00) {
			res = EC_ERROR_UNKNOWN;
			goto exit_wr;
		}
	}

exit_wr:
	/* Relock program lock */
	lock();

	return res;
}

int flash_physical_erase(int offset, int size)
{
	uint32_t *address;
	int res = EC_SUCCESS;

	res = unlock(STM32_FLASH_PECR_PRG_LOCK);
	if (res)
		return res;

	/* Clear previous error status */
	STM32_FLASH_SR = 0xf00;

	/* Set PROG and ERASE bits */
	STM32_FLASH_PECR |= STM32_FLASH_PECR_PROG | STM32_FLASH_PECR_ERASE;

	for (address = (uint32_t *)(CONFIG_FLASH_BASE + offset) ;
	     size > 0; size -= CONFIG_FLASH_ERASE_SIZE,
	     address += CONFIG_FLASH_ERASE_SIZE / sizeof(uint32_t)) {
		timestamp_t deadline;

		/* Do nothing if already erased */
		if (flash_is_erased((uint32_t)address - CONFIG_FLASH_BASE,
				    CONFIG_FLASH_ERASE_SIZE))
			continue;

		/* Start erase */
		*address = 0x00000000;

		/*
		 * Reload the watchdog timer to avoid watchdog reset during
		 * multi-page erase operations.
		 */
		watchdog_reload();

		deadline.val = get_time().val + FLASH_TIMEOUT_MS * MSEC;
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
		 * Check for error conditions: erase failed, voltage error,
		 * protection error
		 */
		if (STM32_FLASH_SR & 0xF00) {
			res = EC_ERROR_UNKNOWN;
			goto exit_er;
		}
	}

exit_er:
	/* Disable program and erase, and relock PECR */
	STM32_FLASH_PECR &= ~(STM32_FLASH_PECR_PROG | STM32_FLASH_PECR_ERASE);
	lock();

	return res;
}

int flash_physical_get_protect(int block)
{
	/* Check the active write protect status */
	return STM32_FLASH_WRPR & (1 << block);
}

int flash_physical_protect_ro_at_boot(int enable)
{
	uint32_t prot;
	uint32_t mask = ((1 << (RO_BANK_COUNT + PSTATE_BANK_COUNT)) - 1)
			<< RO_BANK_OFFSET;
	int rv;

	/* Read the current protection status */
	prot = read_optb_wrp();

	/* Set/clear bits */
	if (enable)
		prot |= mask;
	else
		prot &= ~mask;

	if (prot == read_optb_wrp())
		return EC_SUCCESS;  /* No bits changed */

	/* Unlock option bytes */
	rv = unlock(STM32_FLASH_PECR_OPT_LOCK);
	if (rv)
		return rv;

	/* Update them */
	write_optb_wrp(prot);

	/* Relock */
	lock();

	return EC_SUCCESS;
}

int flash_physical_force_reload(void)
{
	int rv = unlock(STM32_FLASH_PECR_OPT_LOCK);

	if (rv)
		return rv;

	/* Force a reboot; this should never return. */
	STM32_FLASH_PECR = STM32_FLASH_PECR_OBL_LAUNCH;
	while (1)
		;

	return EC_ERROR_UNKNOWN;
}

uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	/*
	 * Try to unlock PECR; if that fails, then all flash is protected for
	 * the current boot.
	 */
	if (unlock(STM32_FLASH_PECR_PE_LOCK))
		flags |= EC_FLASH_PROTECT_ALL_NOW;
	lock();

	return flags;
}

int flash_physical_protect_now(int all)
{
	if (all) {
		/* Re-lock the registers if they're unlocked */
		lock();

		/* Prevent unlocking until reboot */
		ignore_bus_fault(1);
		STM32_FLASH_PEKEYR = 0;
		ignore_bus_fault(0);

		return EC_SUCCESS;
	} else {
		/* No way to protect just the RO flash until next boot */
		return EC_ERROR_INVAL;
	}
}

int flash_pre_init(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	uint32_t prot_flags = flash_get_protect();
	int need_reset = 0;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection. Nothing additional needs to be done.
	 */
	if (reset_flags & RESET_FLAG_SYSJUMP)
		return EC_SUCCESS;

	if (prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
		if ((prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT) &&
		    !(prot_flags & EC_FLASH_PROTECT_RO_NOW)) {
			/*
			 * Pstate wants RO protected at boot, but the write
			 * protect register wasn't set to protect it.  Force an
			 * update to the write protect register and reboot so
			 * it takes effect.
			 */
			flash_protect_ro_at_boot(1);
			need_reset = 1;
		}

		if (prot_flags & EC_FLASH_PROTECT_ERROR_INCONSISTENT) {
			/*
			 * Write protect register was in an inconsistent state.
			 * Set it back to a good state and reboot.
			 */
			flash_protect_ro_at_boot(
				prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT);
			need_reset = 1;
		}
	} else if (prot_flags & (EC_FLASH_PROTECT_RO_NOW |
				 EC_FLASH_PROTECT_ERROR_INCONSISTENT)) {
		/*
		 * Write protect pin unasserted but some section is
		 * protected. Drop it and reboot.
		 */
		unlock(STM32_FLASH_PECR_OPT_LOCK);
		write_optb_wrp(0);
		lock();
		need_reset = 1;
	}

	if (need_reset)
		system_reset(SYSTEM_RESET_HARD | SYSTEM_RESET_PRESERVE_FLAGS);

	return EC_SUCCESS;
}
