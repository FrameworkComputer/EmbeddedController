/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Flash memory module for STM32L4 family */

#include "common.h"
#include "clock.h"
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
#define FLASH_TIMEOUT_US 48000

/*
 * Cros-Ec common flash APIs use the term 'bank' equivalent to how 'page' is
 * used in the STM32 TRMs. Redifining macros here in terms of pages in order to
 * match STM32 documentation for write protect computations in this file.
 *
 * Two write protect (WRP) regions can be defined in the option bytes. The
 * assumption is that 1st WRP area is for RO and the 2nd WRP is for RW if RW WRP
 * config is selected. If RW is being write-protected, it is assume to be the
 * 1st page following the RO section until the last flash page. WRP areas are
 * specified in terms of page indices with a start index and an end index.
 * start == end means a single page is protected.
 *
 *     WRP1a_start = WRP1a_end  --> WRP1a_start page is protected
 *     WRP1a_start > WRP1a_end  --> No WRP area is specified
 *     WRP1a_start < WRP1a_end  --> Pages WRP1a_start to WRP1a_end protected
 *
 * These macros are from the common flash API and mean the following:
 * WP_BANK_OFFSET         -> index of first RO page
 * CONFIG_WP_STORAGE_SIZE -> size of RO region in bytes
 */
#define FLASH_PAGE_SIZE CONFIG_FLASH_BANK_SIZE
#define FLASH_PAGE_MAX_COUNT (CONFIG_FLASH_SIZE / FLASH_PAGE_SIZE)
#define FLASH_RO_FIRST_PAGE_IDX WP_BANK_OFFSET
#define FLASH_RO_LAST_PAGE_IDX ((CONFIG_WP_STORAGE_SIZE / FLASH_PAGE_SIZE) \
				 + FLASH_RO_FIRST_PAGE_IDX - 1)
#define FLASH_RW_FIRST_PAGE_IDX (FLASH_RO_LAST_PAGE_IDX + 1)
#define FLASH_RW_LAST_PAGE_IDX (FLASH_PAGE_MAX_COUNT - 1)


#define FLASH_PAGE_ROLLBACK_COUNT ROLLBACK_BANK_COUNT
#define FLASH_PAGE_ROLLBACK_FIRST_IDX ROLLBACK_BANK_OFFSET
#define FLASH_PAGE_ROLLBACK_LAST_IDX (FLASH_PAGE_ROLLBACK_FIRST_IDX +\
					FLASH_PAGE_ROLLBACK_COUNT -1)

#define FLASH_WRP_MASK              (FLASH_PAGE_MAX_COUNT - 1)
#define FLASH_WRP_START(val)        ((val) & FLASH_WRP_MASK)
#define FLASH_WRP_END(val)          (((val) >> 16) & FLASH_WRP_MASK)
#define FLASH_WRP_RANGE(start, end) (((start) & FLASH_WRP_MASK) | \
				       (((end) & FLASH_WRP_MASK) << 16))
#define FLASH_WRP_RANGE_DISABLED    FLASH_WRP_RANGE(FLASH_WRP_MASK, 0x00)
#define FLASH_WRP1X_MASK FLASH_WRP_RANGE(FLASH_WRP_MASK, FLASH_WRP_MASK)

static inline int calculate_flash_timeout(void)
{
	return (FLASH_TIMEOUT_US *
		(clock_get_freq() / SECOND) / CYCLE_PER_FLASH_LOOP);
}

static int wait_while_busy(void)
{
	int timeout = calculate_flash_timeout();

	while (STM32_FLASH_SR & FLASH_SR_BUSY && timeout-- > 0)
		;
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
	if (STM32_FLASH_CR & FLASH_CR_LOCK) {
		STM32_FLASH_KEYR = FLASH_KEYR_KEY1;
		STM32_FLASH_KEYR = FLASH_KEYR_KEY2;
	}
	/* unlock option memory if required */
	if ((locks & FLASH_CR_OPTLOCK) &&
	    (STM32_FLASH_CR & FLASH_CR_OPTLOCK)) {
		STM32_FLASH_OPTKEYR = FLASH_OPTKEYR_KEY1;
		STM32_FLASH_OPTKEYR = FLASH_OPTKEYR_KEY2;
	}

	/* Re-enable bus fault handler */
	ignore_bus_fault(0);

	return (STM32_FLASH_CR & (locks | FLASH_CR_LOCK)) ? EC_ERROR_UNKNOWN
							  : EC_SUCCESS;
}

static void lock(void)
{
	STM32_FLASH_CR = FLASH_CR_LOCK;
}

/*
 * Option byte organization
 *
 *                [63:56][55:48][47:40][39:32]   [31:24][23:16][15: 8][ 7: 0]
 * +--------------+-------------------+------+   +-------------------+------+
 * | 0x1FFF7800   |         nUSER     | nRDP |   |       USER        |  RDP |
 * +--------------+------------+------+------+   +------------+------+------+
 * | 0x1FFF7808   |            | nPCROP1_STRT|   |            | PCROP1_STRT |
 * +--------------+------------+-------------+   +------------+-------------+
 * | 0x1FFF7810   |            | nPCROP1_END |   |            | PCROP1_END  |
 * +--------------+------------+-------------+   +------------+-------------+
 * | 0x1FFF7818   |     |nWRP1A|      |nWRP1A|   |     | WRP1A|      | WRP1A|
 * |              |     |_END  |      |_STRT |   |     | _END |      | _STRT|
 * +--------------+------------+-------------+   +------------+-------------+
 * | 0x1FFF7820   |     |nWRP1B|      |nWRP1B|   |     | WRP1B|      | WRP1B|
 * |              |     |_END  |      |_STRT |   |     | _END |      | _STRT|
 * +--------------+------------+-------------+   +------------+-------------+
 * | 0x1FFF7828   |     |nBOOT |      |nSEC_ |   |     | BOOT |      | SEC_ |
 * |              |     |LOCK  |      |SIZE1 |   |     | _LOCK|      | SIZE1|
 * +--------------+------------+-------------+   +------------+-------------+
 *
 * Note that the variable with n prefix means the complement.
 */
static int unlock_optb(void)
{
	int rv;

	rv = wait_while_busy();
	if (rv)
		return rv;

	rv = unlock(FLASH_CR_OPTLOCK);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

static int commit_optb(void)
{
	int rv;

	STM32_FLASH_CR |= FLASH_CR_OPTSTRT;

	rv = wait_while_busy();
	if (rv)
		return rv;
	lock();

	return EC_SUCCESS;
}

static void unprotect_all_blocks(void)
{
	unlock_optb();
	STM32_FLASH_WRP1AR = FLASH_WRP_RANGE_DISABLED;
	STM32_FLASH_WRP1BR = FLASH_WRP_RANGE_DISABLED;
	commit_optb();
}

int flash_physical_protect_at_boot(uint32_t new_flags)
{
	uint32_t ro_range = FLASH_WRP_RANGE_DISABLED;
	uint32_t rb_rw_range = FLASH_WRP_RANGE_DISABLED;
	/*
	 * WRP1AR is storing the write-protection range for the RO region.
	 * WRP1BR is storing the write-protection range for the
	 * rollback and RW regions.
	 */
	if (new_flags & (EC_FLASH_PROTECT_ALL_AT_BOOT |
			 EC_FLASH_PROTECT_RO_AT_BOOT))
		ro_range = FLASH_WRP_RANGE(FLASH_RO_FIRST_PAGE_IDX,
					     FLASH_RO_LAST_PAGE_IDX);

	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		rb_rw_range = FLASH_WRP_RANGE(FLASH_RW_FIRST_PAGE_IDX,
						FLASH_RW_LAST_PAGE_IDX);
	} else {
		/*
		 * Start index will be 1st index following RO region index. The
		 * end index is initialized as 'no protect' value. Only if end
		 * gets changed based on either rollback or RW protection will
		 * the 2nd memory protection area get written in option bytes.
		 */
		int start = FLASH_RW_FIRST_PAGE_IDX;
		int end = FLASH_WRP_END(FLASH_WRP_RANGE_DISABLED);
#ifdef CONFIG_ROLLBACK
		if (new_flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
			start = FLASH_PAGE_ROLLBACK_FIRST_IDX;
			end = FLASH_PAGE_ROLLBACK_LAST_IDX;
		} else {
			start = FLASH_PAGE_ROLLBACK_LAST_IDX;
		}
#endif /* !CONFIG_ROLLBACK */
#ifdef CONFIG_FLASH_PROTECT_RW
		if (new_flags & EC_FLASH_PROTECT_RW_AT_BOOT)
			end = FLASH_RW_LAST_PAGE_IDX;
#endif /* CONFIG_FLASH_PROTECT_RW */

		if (end != FLASH_WRP_END(FLASH_WRP_RANGE_DISABLED))
			rb_rw_range = FLASH_WRP_RANGE(start, end);
	}

	unlock_optb();
#ifdef CONFIG_FLASH_READOUT_PROTECTION
	/*
	 * Set a permanent protection by increasing RDP to level 1,
	 * trying to unprotected the flash will trigger a full erase.
	 */
	STM32_FLASH_OPTR = (STM32_FLASH_OPTR & ~0xff) | 0x11;
#endif
	STM32_FLASH_WRP1AR = ro_range;
	STM32_FLASH_WRP1BR = rb_rw_range;
	commit_optb();

	return EC_SUCCESS;
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
	int ro_at_boot = (flags & EC_FLASH_PROTECT_RO_AT_BOOT) ? 1 : 0;
	/* The RO region is write-protected by the WRP1AR range. */
	uint32_t wrp1ar = STM32_OPTB_WRP1AR;
	uint32_t ro_range = ro_at_boot ?
		FLASH_WRP_RANGE(FLASH_RO_FIRST_PAGE_IDX,
				  FLASH_RO_LAST_PAGE_IDX)
		: FLASH_WRP_RANGE_DISABLED;

	return ro_range != (wrp1ar & FLASH_WRP1X_MASK);
}

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_write(int offset, int size, const char *data)
{
	uint32_t *address = (void *)(CONFIG_PROGRAM_MEMORY_BASE + offset);
	int res = EC_SUCCESS;
	int timeout = calculate_flash_timeout();
	int i;
	int unaligned = (uint32_t)data & (STM32_FLASH_MIN_WRITE_SIZE - 1);
	uint32_t *data32 = (void *)data;

	if (unlock(FLASH_CR_LOCK) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/* Clear previous error status */
	STM32_FLASH_SR = FLASH_SR_ERR_MASK;

	/* set PG bit */
	STM32_FLASH_CR |= FLASH_CR_PG;

	for (; size > 0; size -= STM32_FLASH_MIN_WRITE_SIZE) {
		/*
		 * Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing.
		 */
		watchdog_reload();

		/* wait to be ready  */
		for (i = 0; (STM32_FLASH_SR & FLASH_SR_BUSY) && (i < timeout);
		     i++)
			;
		if (STM32_FLASH_SR & FLASH_SR_BUSY) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		/* write the 2 words */
		if (unaligned) {
			*address++ = (uint32_t)data[0] | (data[1] << 8)
				   | (data[2] << 16) | (data[3] << 24);
			*address++ = (uint32_t)data[4] | (data[5] << 8)
				   | (data[6] << 16) | (data[7] << 24);
			data += STM32_FLASH_MIN_WRITE_SIZE;
		} else {
			*address++ = *data32++;
			*address++ = *data32++;
		}

		/* Wait for writes to complete */
		for (i = 0; (STM32_FLASH_SR & FLASH_SR_BUSY) && (i < timeout);
		     i++)
			;

		if (STM32_FLASH_SR & FLASH_SR_BUSY) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		/*
		 * Check for error conditions - erase failed, voltage error,
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

int flash_physical_erase(int offset, int size)
{
	int res = EC_SUCCESS;
	int pg;
	int last;

	if (unlock(FLASH_CR_LOCK) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/* Clear previous error status */
	STM32_FLASH_SR = FLASH_SR_ERR_MASK;

	last = (offset + size) / CONFIG_FLASH_ERASE_SIZE;
	for (pg = offset / CONFIG_FLASH_ERASE_SIZE; pg < last; pg++) {
		timestamp_t deadline;

		/* select page to erase and PER bit */
		STM32_FLASH_CR = (STM32_FLASH_CR & ~FLASH_CR_PNB_MASK)
				| FLASH_CR_PER | FLASH_CR_PNB(pg);

		/* set STRT bit : start erase */
		STM32_FLASH_CR |= FLASH_CR_STRT;

		/*
		 * Reload the watchdog timer to avoid watchdog reset during a
		 * long erase operation.
		 */
		watchdog_reload();

		deadline.val = get_time().val + FLASH_TIMEOUT_US;
		/* Wait for erase to complete */
		while ((STM32_FLASH_SR & FLASH_SR_BUSY) &&
		       (get_time().val < deadline.val)) {
			usleep(300);
		}
		if (STM32_FLASH_SR & FLASH_SR_BUSY) {
			res = EC_ERROR_TIMEOUT;
			goto exit_er;
		}

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
	STM32_FLASH_CR &= ~(FLASH_CR_PER | FLASH_CR_PNB_MASK);

	lock();

	return res;
}

int flash_physical_get_protect(int block)
{
	uint32_t wrp1ar = STM32_FLASH_WRP1AR;
	uint32_t wrp1br = STM32_FLASH_WRP1BR;

	return ((block >= FLASH_WRP_START(wrp1ar)) &&
		(block <= FLASH_WRP_END(wrp1ar))) ||
		((block >= FLASH_WRP_START(wrp1br)) &&
		(block <= FLASH_WRP_END(wrp1br)));
}

/*
 * Note: This does not need to update _NOW flags, as get_protect_flags
 * in common code already does so.
 */
uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;
	uint32_t wrp1ar = STM32_OPTB_WRP1AR;
	uint32_t wrp1br = STM32_OPTB_WRP1BR;

	/* RO region protection range is in WRP1AR range */
	if (wrp1ar == FLASH_WRP_RANGE(FLASH_RO_FIRST_PAGE_IDX,
					FLASH_RO_LAST_PAGE_IDX))
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
	/* Rollback and RW regions protection range is in WRP1BR range */
	if (wrp1br != FLASH_WRP_RANGE_DISABLED) {
		int end = FLASH_WRP_END(wrp1br);
		int strt = FLASH_WRP_START(wrp1br);

#ifdef CONFIG_ROLLBACK
		if (strt <= FLASH_PAGE_ROLLBACK_FIRST_IDX &&
		    end >= FLASH_PAGE_ROLLBACK_LAST_IDX)
			flags |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif /* CONFIG_ROLLBACK */
#ifdef CONFIG_FLASH_PROTECT_RW
		if (end == PHYSICAL_BANKS)
			flags |= EC_FLASH_PROTECT_RW_AT_BOOT;
#endif /* CONFIG_FLASH_PROTECT_RW */
		if (end == PHYSICAL_BANKS &&
		    strt == WP_BANK_OFFSET + WP_BANK_COUNT &&
		    flags & EC_FLASH_PROTECT_RO_AT_BOOT)
			flags |= EC_FLASH_PROTECT_ALL_AT_BOOT;
	}

	return flags;
}

int flash_physical_protect_now(int all)
{
	return EC_ERROR_INVAL;
}

uint32_t flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT |
	       EC_FLASH_PROTECT_RO_NOW |
#ifdef CONFIG_FLASH_PROTECT_RW
	       EC_FLASH_PROTECT_RW_AT_BOOT |
	       EC_FLASH_PROTECT_RW_NOW |
#endif
#ifdef CONFIG_ROLLBACK
	       EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
	       EC_FLASH_PROTECT_ROLLBACK_NOW |
#endif
	       EC_FLASH_PROTECT_ALL_AT_BOOT |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * ALL/RW at-boot state can be set if WP GPIO is asserted and can always
	 * be cleared.
	 */
	if (cur_flags & (EC_FLASH_PROTECT_ALL_AT_BOOT |
			 EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_AT_BOOT;

#ifdef CONFIG_FLASH_PROTECT_RW
	if (cur_flags & (EC_FLASH_PROTECT_RW_AT_BOOT |
			 EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_RW_AT_BOOT;
#endif

#ifdef CONFIG_ROLLBACK
	if (cur_flags & (EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
			 EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif

	return ret;
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
	if (reset_flags & EC_RESET_FLAG_SYSJUMP)
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
			flash_physical_protect_at_boot(
				EC_FLASH_PROTECT_RO_AT_BOOT);
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
			flash_protect_at_boot(
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

	if ((flash_physical_get_valid_flags() & EC_FLASH_PROTECT_ALL_AT_BOOT) &&
	    (!!(prot_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) !=
	     !!(prot_flags & EC_FLASH_PROTECT_ALL_NOW))) {
		/*
		 * ALL_AT_BOOT and ALL_NOW should be both set or both unset
		 * at boot. If they are not, it must be that the chip requires
		 * OBL_LAUNCH to be set to reload option bytes. Let's reset
		 * the system with OBL_LAUNCH set.
		 * This assumes OBL_LAUNCH is used for hard reset in
		 * chip/stm32/system.c.
		 */
		need_reset = 1;
	}

#ifdef CONFIG_FLASH_PROTECT_RW
	if ((flash_physical_get_valid_flags() & EC_FLASH_PROTECT_RW_AT_BOOT) &&
	    (!!(prot_flags & EC_FLASH_PROTECT_RW_AT_BOOT) !=
	     !!(prot_flags & EC_FLASH_PROTECT_RW_NOW))) {
		/* RW_AT_BOOT and RW_NOW do not match. */
		need_reset = 1;
	}
#endif

#ifdef CONFIG_ROLLBACK
	if ((flash_physical_get_valid_flags() &
	     EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) &&
	    (!!(prot_flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) !=
	     !!(prot_flags & EC_FLASH_PROTECT_ROLLBACK_NOW))) {
		/* ROLLBACK_AT_BOOT and ROLLBACK_NOW do not match. */
		need_reset = 1;
	}
#endif

	if (need_reset)
		system_reset(SYSTEM_RESET_HARD | SYSTEM_RESET_PRESERVE_FLAGS);

	return EC_SUCCESS;
}
