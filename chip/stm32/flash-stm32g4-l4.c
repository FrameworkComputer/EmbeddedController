/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Flash memory module for STM32L4 family */

#include "clock.h"
#include "common.h"
#include "flash.h"
#include "hooks.h"
#include "panic.h"
#include "registers.h"
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
 * These macros are from the common flash API and mean the following:
 * WP_BANK_OFFSET         -> index of first RO page
 * CONFIG_WP_STORAGE_SIZE -> size of RO region in bytes
 */
#define FLASH_PAGE_SIZE CONFIG_FLASH_BANK_SIZE
#define FLASH_PAGE_MAX_COUNT (CONFIG_FLASH_SIZE_BYTES / FLASH_PAGE_SIZE)
#define FLASH_RO_FIRST_PAGE_IDX WP_BANK_OFFSET
#define FLASH_RO_LAST_PAGE_IDX                        \
	((CONFIG_WP_STORAGE_SIZE / FLASH_PAGE_SIZE) + \
	 FLASH_RO_FIRST_PAGE_IDX - 1)
#define FLASH_RW_FIRST_PAGE_IDX (FLASH_RO_LAST_PAGE_IDX + 1)
#define FLASH_RW_LAST_PAGE_IDX (FLASH_PAGE_MAX_COUNT - 1)

#define FLASH_PAGE_ROLLBACK_COUNT ROLLBACK_BANK_COUNT
#define FLASH_PAGE_ROLLBACK_FIRST_IDX ROLLBACK_BANK_OFFSET
#define FLASH_PAGE_ROLLBACK_LAST_IDX \
	(FLASH_PAGE_ROLLBACK_FIRST_IDX + FLASH_PAGE_ROLLBACK_COUNT - 1)

#ifdef STM32_FLASH_DBANK_MODE
#define FLASH_WRP_MASK (FLASH_PAGE_MAX_COUNT - 1)
#else
#ifdef CHIP_FAMILY_STM32L4
#define FLASH_WRP_MASK 0xFF
#else
#define FLASH_WRP_MASK ((FLASH_PAGE_MAX_COUNT) / 2 - 1)
#endif
#endif /* CONFIG_FLASH_DBANK_MODE */
#define FLASH_WRP_START(val) ((val) & FLASH_WRP_MASK)
#define FLASH_WRP_END(val) (((val) >> 16) & FLASH_WRP_MASK)
#define FLASH_WRP_RANGE(start, end) \
	(((start) & FLASH_WRP_MASK) | (((end) & FLASH_WRP_MASK) << 16))
#define FLASH_WRP_RANGE_DISABLED FLASH_WRP_RANGE(FLASH_WRP_MASK, 0x00)
#define FLASH_WRP1X_MASK FLASH_WRP_RANGE(FLASH_WRP_MASK, FLASH_WRP_MASK)

enum wrp_region {
	WRP_RO,
	WRP_RW,
};

struct wrp_info {
	int enable;
	int start;
	int end;
};

static inline int calculate_flash_timeout(void)
{
	return (FLASH_TIMEOUT_US * (clock_get_freq() / SECOND) /
		CYCLE_PER_FLASH_LOOP);
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
	if ((locks & FLASH_CR_OPTLOCK) && (STM32_FLASH_CR & FLASH_CR_OPTLOCK)) {
		STM32_FLASH_OPTKEYR = FLASH_OPTKEYR_KEY1;
		STM32_FLASH_OPTKEYR = FLASH_OPTKEYR_KEY2;
	}

	/* Re-enable bus fault handler */
	ignore_bus_fault(0);

	return (STM32_FLASH_CR & (locks | FLASH_CR_LOCK)) ? EC_ERROR_UNKNOWN :
							    EC_SUCCESS;
}

static void lock(void)
{
	STM32_FLASH_CR |= FLASH_CR_LOCK;
}

static void ob_lock(void)
{
	STM32_FLASH_CR |= FLASH_CR_OPTLOCK;
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

	/*
	 * Wait for last operation.
	 */
	rv = wait_while_busy();
	if (rv)
		return rv;

	STM32_FLASH_CR |= FLASH_CR_OPTSTRT;

	rv = wait_while_busy();
	if (rv)
		return rv;

	ob_lock();
	lock();

	return EC_SUCCESS;
}

/*
 * There are a minimum of 2 WRP regions that can be set. The STM32G4
 * family has both category 2, and category 3 devices. Category 2
 * devices have only 2 WRP regions, but category 3 devices have 4 WRP
 * regions that can be configured. Category 3 devices also support dual
 * flash banks, and this mode is the default setting. When DB mode is enabled,
 * then each WRP register can only protect up to 64 2kB pages. This means that
 * one WRP register is needed per bank.
 *
 *   1. WRP1A -> used always for RO
 *   2. WRP1B -> used always for RW
 *   3. WRP2A -> may be used for RW if dual-bank (DB) mode is enabled
 *   4. WRP2B -> currently never used
 *
 * WRP areas are specified in terms of page indices with a start index
 * and an end index. start == end means a single page is protected.
 *
 *   WRPnx_start = WRPnx_end  --> WRPnx_start page is protected
 *   WRPnx_start > WRPnx_end  --> No WRP area is specified
 *   WRPnx_start < WRPnx_end  --> Pages WRPnx_start to WRPnx_end
 */
static void optb_get_wrp(enum wrp_region region, struct wrp_info *wrp)
{
#ifdef STM32_FLASH_DBANK_MODE
	int start;
	int end;
#endif
	/* Assume WRP regions are not configured */
	wrp->start = FLASH_WRP_MASK;
	wrp->end = 0;
	wrp->enable = 0;

	if (region == WRP_RO) {
		/*
		 * RO write protect is fully described by WRP1AR. Get the
		 * start/end indices. If end >= start, then RO write protect is
		 * enabled.
		 */
		wrp->start = FLASH_WRP_START(STM32_OPTB_WRP1AR);
		wrp->end = FLASH_WRP_END(STM32_OPTB_WRP1AR);
		wrp->enable = wrp->end >= wrp->start;
	} else if (region == WRP_RW) {
		/*
		 * RW write always uses WRP1BR. If dual-bank mode is being used,
		 * then WRP2AR must also be check to determine the full range of
		 * flash page indices being protected.
		 */
		wrp->start = FLASH_WRP_START(STM32_OPTB_WRP1BR);
		wrp->end = FLASH_WRP_END(STM32_OPTB_WRP1BR);
		wrp->enable = wrp->end >= wrp->start;
#ifdef STM32_FLASH_DBANK_MODE
		start = FLASH_WRP_START(STM32_FLASH_WRP2AR);
		end = FLASH_WRP_END(STM32_FLASH_WRP2AR);
		/*
		 * If WRP2AR protection is enabled, then need to adjust either
		 * the start, end, or both indices.
		 */
		if (end >= start) {
			if (wrp->enable) {
				/* WRP1BR is active, only need to adjust end */
				wrp->end += end;
			} else {
				/*
				 * WRP1BR is not active, so RW protection, if
				 * enabled, is fully controlled by WRP2AR.
				 */
				wrp->start = start;
				wrp->end = end;
				wrp->enable = 1;
			}
		}
#endif
	}
}

static void optb_set_wrp(enum wrp_region region, struct wrp_info *wrp)
{
	int start = wrp->start;
	int end = wrp->end;

	if (!wrp->enable) {
		/*
		 * If enable is not set, then ignore the passed in start/end
		 * values and set start/end to the default not protected range
		 * which satisfies start > end
		 */
		start = FLASH_WRP_MASK;
		end = 0;
	}

	if (region == WRP_RO) {
		/* For RO can always use start/end directly */
		STM32_FLASH_WRP1AR = FLASH_WRP_RANGE(start, end);
	} else if (region == WRP_RW) {
#ifdef STM32_FLASH_DBANK_MODE
		/*
		 * In the dual-bank flash case (STM32G4 Category 3 devices with
		 * DB bit set), RW write protect can use both WRP1BR and WRP2AR
		 * registers in order to span the full flash memory range.
		 */
		if (wrp->enable) {
			int rw_end;

			/*
			 * If the 1st RW flash page is in the 1st half of
			 * memory, then at least one block will be protected by
			 * WRP1BR. If the end flash page is in the 2nd half of
			 * memory, then cap the end for WRP1BR to its max
			 * value. Otherwise, can use end passed in directly.
			 */
			if (start <= FLASH_WRP_MASK) {
				rw_end = end > FLASH_WRP_MASK ? FLASH_WRP_MASK :
								end;
				STM32_FLASH_WRP1BR =
					FLASH_WRP_RANGE(start, rw_end);
			}
			/*
			 * If the last RW flash page is in the 2nd half of
			 * memory, then at least one block will be protected by
			 * WRP2AR. If the start flash page is in the 2nd half of
			 * memory, can use start directly. Otherwise, start
			 * needs to be set to 0 here.
			 */
			if (end > FLASH_WRP_MASK) {
				rw_end = end & FLASH_WRP_MASK;
				STM32_FLASH_WRP2AR = FLASH_WRP_RANGE(0, rw_end);
			}
		} else {
			/*
			 * RW write protect is being disabled. Set both WRP1BR
			 * and WRP2AR to default start > end not protected
			 * state.
			 */
			STM32_FLASH_WRP1BR = FLASH_WRP_RANGE(start, end);
			STM32_FLASH_WRP2AR = FLASH_WRP_RANGE(start, end);
		}
#else
		/* Single bank case, WRP1BR can cover the full memory range */
		STM32_FLASH_WRP1BR = FLASH_WRP_RANGE(start, end);
#endif
	}
}

static void unprotect_all_blocks(void)
{
	struct wrp_info wrp;

	/* Set info values to unprotected */
	wrp.start = FLASH_WRP_MASK;
	wrp.end = 0;
	wrp.enable = 0;

	unlock_optb();
	/* Disable RO WRP */
	optb_set_wrp(WRP_RO, &wrp);
	/* Disable RW WRP */
	optb_set_wrp(WRP_RW, &wrp);
	commit_optb();
}

int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	struct wrp_info wrp_ro;
	struct wrp_info wrp_rw;

	wrp_ro.start = FLASH_WRP_MASK;
	wrp_ro.end = 0;
	wrp_ro.enable = 0;

	wrp_rw.start = FLASH_WRP_MASK;
	wrp_rw.end = 0;
	wrp_rw.enable = 0;

	/*
	 * Default operation for this function is to disable both RO and RW
	 * write protection in the option bytes. Based on new_flags either RO or
	 * RW or both regions write protect may be set.
	 */
	if (new_flags &
	    (EC_FLASH_PROTECT_ALL_AT_BOOT | EC_FLASH_PROTECT_RO_AT_BOOT)) {
		wrp_ro.start = FLASH_RO_FIRST_PAGE_IDX;
		wrp_ro.end = FLASH_RO_LAST_PAGE_IDX;
		wrp_ro.enable = 1;
	}

	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		wrp_rw.start = FLASH_RW_FIRST_PAGE_IDX;
		wrp_rw.end = FLASH_RW_LAST_PAGE_IDX;
		wrp_rw.enable = 1;
	} else {
		/*
		 * Start index will be 1st index following RO region index. The
		 * end index is initialized as 'no protect' value. Only if end
		 * gets changed based on either rollback or RW protection will
		 * the 2nd memory protection area get written in option bytes.
		 */
		int start = FLASH_RW_FIRST_PAGE_IDX;
		int end = 0;
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

		if (end) {
			wrp_rw.start = start;
			wrp_rw.end = end;
			wrp_rw.enable = 1;
		}
	}

	unlock_optb();
#ifdef CONFIG_FLASH_READOUT_PROTECTION
	/*
	 * Set a permanent protection by increasing RDP to level 1,
	 * trying to unprotected the flash will trigger a full erase.
	 */
	STM32_FLASH_OPTR = (STM32_FLASH_OPTR & ~0xff) | 0x11;
#endif
	optb_set_wrp(WRP_RO, &wrp_ro);
	optb_set_wrp(WRP_RW, &wrp_rw);
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
	uint32_t flags = crec_flash_get_protect();
	int ro_at_boot = (flags & EC_FLASH_PROTECT_RO_AT_BOOT) ? 1 : 0;
	/* The RO region is write-protected by the WRP1AR range. */
	uint32_t wrp1ar = STM32_OPTB_WRP1AR;
	uint32_t ro_range = ro_at_boot ?
				    FLASH_WRP_RANGE(FLASH_RO_FIRST_PAGE_IDX,
						    FLASH_RO_LAST_PAGE_IDX) :
				    FLASH_WRP_RANGE_DISABLED;

	return ro_range != (wrp1ar & FLASH_WRP1X_MASK);
}

/*****************************************************************************/
/* Physical layer APIs */

int crec_flash_physical_write(int offset, int size, const char *data)
{
	uint32_t *address = (void *)(CONFIG_PROGRAM_MEMORY_BASE + offset);
	int res = EC_SUCCESS;
	int timeout = calculate_flash_timeout();
	int i;
	int unaligned = (uint32_t)data & (STM32_FLASH_MIN_WRITE_SIZE - 1);
	uint32_t *data32 = (void *)data;

	/* Check Flash offset */
	if (offset % STM32_FLASH_MIN_WRITE_SIZE)
		return EC_ERROR_MEMORY_ALLOCATION;

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
			*address++ = (uint32_t)data[0] | (data[1] << 8) |
				     (data[2] << 16) | (data[3] << 24);
			*address++ = (uint32_t)data[4] | (data[5] << 8) |
				     (data[6] << 16) | (data[7] << 24);
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

int crec_flash_physical_erase(int offset, int size)
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
		STM32_FLASH_CR = (STM32_FLASH_CR & ~FLASH_CR_PNB_MASK) |
				 FLASH_CR_PER | FLASH_CR_PNB(pg);

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

int crec_flash_physical_get_protect(int block)
{
	struct wrp_info wrp_ro;
	struct wrp_info wrp_rw;

	optb_get_wrp(WRP_RO, &wrp_ro);
	optb_get_wrp(WRP_RW, &wrp_rw);

	return ((block >= wrp_ro.start) && (block <= wrp_ro.end)) ||
	       ((block >= wrp_rw.start) && (block <= wrp_rw.end));
}

/*
 * Note: This does not need to update _NOW flags, as get_protect_flags
 * in common code already does so.
 */
uint32_t crec_flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;
	struct wrp_info wrp_ro;
	struct wrp_info wrp_rw;

	optb_get_wrp(WRP_RO, &wrp_ro);
	optb_get_wrp(WRP_RW, &wrp_rw);

	/* Check if RO is fully protected */
	if (wrp_ro.start == FLASH_RO_FIRST_PAGE_IDX &&
	    wrp_ro.end == FLASH_RO_LAST_PAGE_IDX)
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;

	if (wrp_rw.enable) {
#ifdef CONFIG_ROLLBACK
		if (wrp_rw.start <= FLASH_PAGE_ROLLBACK_FIRST_IDX &&
		    wrp_rw.end >= FLASH_PAGE_ROLLBACK_LAST_IDX)
			flags |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif /* CONFIG_ROLLBACK */
#ifdef CONFIG_FLASH_PROTECT_RW
		if (wrp_rw.end == PHYSICAL_BANKS)
			flags |= EC_FLASH_PROTECT_RW_AT_BOOT;
#endif /* CONFIG_FLASH_PROTECT_RW */
		if (wrp_rw.end == PHYSICAL_BANKS &&
		    wrp_rw.start == WP_BANK_OFFSET + WP_BANK_COUNT &&
		    flags & EC_FLASH_PROTECT_RO_AT_BOOT)
			flags |= EC_FLASH_PROTECT_ALL_AT_BOOT;
	}

	return flags;
}

int crec_flash_physical_protect_now(int all)
{
	return EC_ERROR_INVAL;
}

uint32_t crec_flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
#ifdef CONFIG_FLASH_PROTECT_RW
	       EC_FLASH_PROTECT_RW_AT_BOOT | EC_FLASH_PROTECT_RW_NOW |
#endif
#ifdef CONFIG_ROLLBACK
	       EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
	       EC_FLASH_PROTECT_ROLLBACK_NOW |
#endif
	       EC_FLASH_PROTECT_ALL_AT_BOOT | EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t crec_flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * ALL/RW at-boot state can be set if WP GPIO is asserted and can always
	 * be cleared.
	 */
	if (cur_flags &
	    (EC_FLASH_PROTECT_ALL_AT_BOOT | EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_AT_BOOT;

#ifdef CONFIG_FLASH_PROTECT_RW
	if (cur_flags &
	    (EC_FLASH_PROTECT_RW_AT_BOOT | EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_RW_AT_BOOT;
#endif

#ifdef CONFIG_ROLLBACK
	if (cur_flags & (EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
			 EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif

	return ret;
}

int crec_flash_physical_force_reload(void)
{
	int rv = unlock(FLASH_CR_OPTLOCK);

	if (rv)
		return rv;

	/* Force a reboot; this should never return. */
	STM32_FLASH_CR = FLASH_CR_OBL_LAUNCH;
	while (1)
		;

	return EC_ERROR_UNKNOWN;
}

int crec_flash_pre_init(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	uint32_t prot_flags = crec_flash_get_protect();
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
			crec_flash_physical_protect_at_boot(
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
			crec_flash_protect_at_boot(prot_flags &
						   EC_FLASH_PROTECT_RO_AT_BOOT);
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

	if ((crec_flash_physical_get_valid_flags() &
	     EC_FLASH_PROTECT_ALL_AT_BOOT) &&
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
	if ((crec_flash_physical_get_valid_flags() &
	     EC_FLASH_PROTECT_RW_AT_BOOT) &&
	    (!!(prot_flags & EC_FLASH_PROTECT_RW_AT_BOOT) !=
	     !!(prot_flags & EC_FLASH_PROTECT_RW_NOW))) {
		/* RW_AT_BOOT and RW_NOW do not match. */
		need_reset = 1;
	}
#endif

#ifdef CONFIG_ROLLBACK
	if ((crec_flash_physical_get_valid_flags() &
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
