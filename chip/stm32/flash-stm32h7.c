/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Flash memory module for STM32H7 family */

#include "clock.h"
#include "common.h"
#include "cpu.h"
#include "flash-regs.h"
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
#define CYCLE_PER_FLASH_LOOP 2

/* Flash 256-bit word programming timeout. */
#define FLASH_TIMEOUT_US 600

/*
 * Flash 128-KB block erase timeout.
 * Datasheet says maximum is about 4 seconds in x8.
 * Real delay seems to be: < 1 second in x64, < 2 seconds in x8.
 */
#define FLASH_ERASE_TIMEOUT_US (4200 * MSEC)

/*
 * Option bytes programming timeout.
 * No specification, real delay seems to be around 300ms.
 */
#define FLASH_OPT_PRG_TIMEOUT_US (1000 * MSEC)

/*
 * All variants have 2 banks (as in parallel hardware / controllers)
 * not what is called 'bank' in the common code (ie Write-Protect sectors)
 * both have the same number of 128KB blocks.
 */
#define HWBANK_SIZE (CONFIG_FLASH_SIZE_BYTES / 2)
#define BLOCKS_PER_HWBANK (HWBANK_SIZE / CONFIG_FLASH_ERASE_SIZE)
#define BLOCKS_HWBANK_MASK (BIT(BLOCKS_PER_HWBANK) - 1)

/*
 * We can tune the power consumption vs erase/write speed
 * by default, go fast (and consume current)
 */
#define DEFAULT_PSIZE FLASH_CR_PSIZE_DWORD

/* Can no longer write/erase flash until next reboot */
static int access_disabled;
/* Can no longer modify write-protection in option bytes until next reboot */
static int option_disabled;
/* Is physical flash stuck protected? (avoid reboot loop) */
static int stuck_locked;

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */
#define FLASH_HOOK_VERSION 1

/* The previous write protect state before sys jump */
struct flash_wp_state {
	int access_disabled;
	int option_disabled;
	int stuck_locked;
};

static inline int calculate_flash_timeout(void)
{
	return (FLASH_TIMEOUT_US * (clock_get_freq() / SECOND) /
		CYCLE_PER_FLASH_LOOP);
}

static int unlock(int bank)
{
	/* unlock CR only if needed */
	if (STM32_FLASH_CR(bank) & FLASH_CR_LOCK) {
		/*
		 * We may have already locked the flash module and get a bus
		 * fault in the attempt to unlock. Need to disable bus fault
		 * handler now.
		 */
		ignore_bus_fault(1);

		STM32_FLASH_KEYR(bank) = FLASH_KEYR_KEY1;
		STM32_FLASH_KEYR(bank) = FLASH_KEYR_KEY2;
		ignore_bus_fault(0);
	}

	return (STM32_FLASH_CR(bank) & FLASH_CR_LOCK) ? EC_ERROR_UNKNOWN :
							EC_SUCCESS;
}

static void lock(int bank)
{
	STM32_FLASH_CR(bank) |= FLASH_CR_LOCK;
}

static int unlock_optb(void)
{
	if (option_disabled)
		return EC_ERROR_ACCESS_DENIED;

	if (unlock(0))
		return EC_ERROR_UNKNOWN;

	if (flash_option_bytes_locked()) {
		/*
		 * We may have already locked the flash module and get a bus
		 * fault in the attempt to unlock. Need to disable bus fault
		 * handler now.
		 */
		ignore_bus_fault(1);

		unlock_flash_option_bytes();
		ignore_bus_fault(0);
	}

	return flash_option_bytes_locked() ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}

static int commit_optb(void)
{
	/* might use this before timer_init, cannot use get_time/usleep */
	int timeout = (FLASH_OPT_PRG_TIMEOUT_US * (clock_get_freq() / SECOND) /
		       CYCLE_PER_FLASH_LOOP);

	STM32_FLASH_OPTCR(0) |= FLASH_OPTCR_OPTSTART;

	while (STM32_FLASH_OPTSR_CUR(0) & FLASH_OPTSR_BUSY && timeout-- > 0)
		;

	lock_flash_option_bytes();
	lock(0);

	return (timeout > 0) ? EC_SUCCESS : EC_ERROR_TIMEOUT;
}

static void protect_blocks(uint32_t blocks)
{
	if (unlock_optb())
		return;
	STM32_FLASH_WPSN_PRG(0) &= ~(blocks & BLOCKS_HWBANK_MASK);
	STM32_FLASH_WPSN_PRG(1) &=
		~((blocks >> BLOCKS_PER_HWBANK) & BLOCKS_HWBANK_MASK);
	commit_optb();
}

/*
 * Helper function definitions for consistency with F4 to enable flash
 * physical unitesting
 */
void unlock_flash_control_register(void)
{
	unlock(0);
	unlock(1);
}

void unlock_flash_option_bytes(void)
{
	/*
	 * Always use bank 0 flash controller as there is only one option bytes
	 * set for both banks. See http://b/181130245
	 *
	 * Consecutively program values. Ref: RM0433:4.9.2
	 */
	STM32_FLASH_OPTKEYR(0) = FLASH_OPTKEYR_KEY1;
	STM32_FLASH_OPTKEYR(0) = FLASH_OPTKEYR_KEY2;
}

void disable_flash_option_bytes(void)
{
	ignore_bus_fault(1);
	/*
	 * Always use bank 0 flash controller as there is only one option bytes
	 * set for both banks. See http://b/181130245
	 *
	 * Writing anything other than the pre-defined keys to the option key
	 * register results in a bus fault and the register being locked until
	 * reboot (even with a further correct key write).
	 */
	STM32_FLASH_OPTKEYR(0) = 0xffffffff;
	ignore_bus_fault(0);
}

void disable_flash_control_register(void)
{
	ignore_bus_fault(1);
	/*
	 * Writing anything other than the pre-defined keys to a key
	 * register results in a bus fault and the register being locked until
	 * reboot (even with a further correct key write).
	 */
	STM32_FLASH_KEYR(0) = 0xffffffff;
	STM32_FLASH_KEYR(1) = 0xffffffff;
	ignore_bus_fault(0);
}

void lock_flash_control_register(void)
{
	lock(0);
	lock(1);
}

void lock_flash_option_bytes(void)
{
	/*
	 * Always use bank 0 flash controller as there is only one option bytes
	 * set for both banks. See http://b/181130245
	 */
	STM32_FLASH_OPTCR(0) |= FLASH_OPTCR_OPTLOCK;
}

bool flash_option_bytes_locked(void)
{
	/*
	 * Always use bank 0 flash controller as there is only one option bytes
	 * set for both banks. See http://b/181130245
	 */
	return !!(STM32_FLASH_OPTCR(0) & FLASH_OPTCR_OPTLOCK);
}

bool flash_control_register_locked(void)
{
	return !!(STM32_FLASH_CR(0) & FLASH_CR_LOCK) &&
	       !!(STM32_FLASH_CR(1) & FLASH_CR_LOCK);
}

/*
 * If RDP as PSTATE option is defined, use that as 'Write Protect enabled' flag:
 * it makes no sense to be able to unlock RO, as that'd allow flashing
 * arbitrary RO that could read back all flash.
 *
 * crbug.com/888109: Do not copy this code over to other STM32 chips without
 * understanding the full implications.
 *
 * If RDP is not defined, use the option bytes RSS1 bit.
 * TODO(crbug.com/888104): Validate that using RSS1 for this purpose is safe.
 */
#ifndef CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE
#error "crbug.com/888104: Using RSS1 for write protect PSTATE may not be safe."
#endif
static int is_wp_enabled(void)
{
#ifdef CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE
	return (STM32_FLASH_OPTSR_CUR(0) & FLASH_OPTSR_RDP_MASK) !=
	       FLASH_OPTSR_RDP_LEVEL_0;
#else
	return !!(STM32_FLASH_OPTSR_CUR(0) & FLASH_OPTSR_RSS1);
#endif
}

static int set_wp(int enabled)
{
	int rv;

	rv = unlock_optb();
	if (rv)
		return rv;

#ifdef CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE
	if (enabled) {
		/* Enable RDP level 1. */
		STM32_FLASH_OPTSR_PRG(0) =
			(STM32_FLASH_OPTSR_PRG(0) & ~FLASH_OPTSR_RDP_MASK) |
			FLASH_OPTSR_RDP_LEVEL_1;
	}
#else
	if (enabled)
		STM32_FLASH_OPTSR_PRG(0) |= FLASH_OPTSR_RSS1;
	else
		STM32_FLASH_OPTSR_PRG(0) &= ~FLASH_OPTSR_RSS1;
#endif

	return commit_optb();
}

/*****************************************************************************/
/* Physical layer APIs */

int crec_flash_physical_write(int offset, int size, const char *data)
{
	int res = EC_SUCCESS;
	int bank = offset / HWBANK_SIZE;
	uint32_t *address = (void *)(CONFIG_PROGRAM_MEMORY_BASE + offset);
	int timeout = calculate_flash_timeout();
	int i;
	int unaligned = (uint32_t)data & (CONFIG_FLASH_WRITE_SIZE - 1);
	uint32_t *data32 = (void *)data;

	if (access_disabled)
		return EC_ERROR_ACCESS_DENIED;

	/* work on a single hardware bank at a time */
	if ((offset + size - 1) / HWBANK_SIZE != bank)
		return EC_ERROR_INVAL;

	if (unlock(bank) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/* Clear previous error status */
	STM32_FLASH_CCR(bank) = FLASH_CCR_ERR_MASK;

	/* select write parallelism */
	STM32_FLASH_CR(bank) = (STM32_FLASH_CR(bank) & ~FLASH_CR_PSIZE_MASK) |
			       DEFAULT_PSIZE;

	/* set PG bit */
	STM32_FLASH_CR(bank) |= FLASH_CR_PG;

	for (; size > 0; size -= CONFIG_FLASH_WRITE_SIZE) {
		/*
		 * Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing.
		 */
		watchdog_reload();

		/* write a 256-bit flash word */
		if (unaligned) {
			for (i = 0; i < CONFIG_FLASH_WRITE_SIZE / 4;
			     i++, data += 4)
				*address++ = (uint32_t)data[0] |
					     (data[1] << 8) | (data[2] << 16) |
					     (data[3] << 24);
		} else {
			for (i = 0; i < CONFIG_FLASH_WRITE_SIZE / 4; i++)
				*address++ = *data32++;
		}

		/* Wait for writes to complete */
		for (i = 0;
		     (STM32_FLASH_SR(bank) & (FLASH_SR_WBNE | FLASH_SR_QW)) &&
		     (i < timeout);
		     i++)
			;

		if (STM32_FLASH_SR(bank) & (FLASH_SR_WBNE | FLASH_SR_QW)) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		if (STM32_FLASH_SR(bank) & FLASH_CCR_ERR_MASK) {
			res = EC_ERROR_UNKNOWN;
			goto exit_wr;
		}
	}

exit_wr:
	/* Disable PG bit */
	STM32_FLASH_CR(bank) &= ~FLASH_CR_PG;

	lock(bank);

#ifdef CONFIG_ARMV7M_CACHE
	/* Invalidate D-cache, to make sure we do not read back stale data. */
	cpu_clean_invalidate_dcache();
#endif

	return res;
}

int crec_flash_physical_erase(int offset, int size)
{
	int res = EC_SUCCESS;
	int bank = offset / HWBANK_SIZE;
	int last = (offset + size) / CONFIG_FLASH_ERASE_SIZE;
	int sect;

	if (access_disabled)
		return EC_ERROR_ACCESS_DENIED;

	/* work on a single hardware bank at a time */
	if ((offset + size - 1) / HWBANK_SIZE != bank)
		return EC_ERROR_INVAL;

	if (unlock(bank) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/* Clear previous error status */
	STM32_FLASH_CCR(bank) = FLASH_CCR_ERR_MASK;

	/* select erase parallelism */
	STM32_FLASH_CR(bank) = (STM32_FLASH_CR(bank) & ~FLASH_CR_PSIZE_MASK) |
			       DEFAULT_PSIZE;

	for (sect = offset / CONFIG_FLASH_ERASE_SIZE; sect < last; sect++) {
		timestamp_t deadline;

		/* select page to erase and PER bit */
		STM32_FLASH_CR(bank) =
			(STM32_FLASH_CR(bank) & ~FLASH_CR_SNB_MASK) |
			FLASH_CR_SER | FLASH_CR_SNB(sect);

		/* set STRT bit : start erase */
		STM32_FLASH_CR(bank) |= FLASH_CR_STRT;

		/*
		 * Reload the watchdog timer to avoid watchdog reset during a
		 * long erase operation.
		 */
		watchdog_reload();

		deadline.val = get_time().val + FLASH_ERASE_TIMEOUT_US;
		/* Wait for erase to complete */
		while ((STM32_FLASH_SR(bank) & FLASH_SR_BUSY) &&
		       (get_time().val < deadline.val)) {
			/*
			 * Interrupts may not be enabled, so we are using
			 * udelay() instead of crec_usleep() which can trigger
			 * Forced Hard Fault (see b/180761547).
			 */
			udelay(5000);
		}
		if (STM32_FLASH_SR(bank) & FLASH_SR_BUSY) {
			res = EC_ERROR_TIMEOUT;
			goto exit_er;
		}

		/*
		 * Check for error conditions - erase failed, voltage error,
		 * protection error
		 */
		if (STM32_FLASH_SR(bank) & FLASH_CCR_ERR_MASK) {
			res = EC_ERROR_UNKNOWN;
			goto exit_er;
		}
	}

exit_er:
	/* reset SER bit */
	STM32_FLASH_CR(bank) &= ~(FLASH_CR_SER | FLASH_CR_SNB_MASK);

	lock(bank);

#ifdef CONFIG_ARMV7M_CACHE
	/* Invalidate D-cache, to make sure we do not read back stale data. */
	cpu_clean_invalidate_dcache();
#endif

	return res;
}

int crec_flash_physical_get_protect(int block)
{
	int bank = block / BLOCKS_PER_HWBANK;
	int index = block % BLOCKS_PER_HWBANK;

	return !(STM32_FLASH_WPSN_CUR(bank) & BIT(index));
}

/*
 * Note: This does not need to update _NOW flags, as flash_get_protect
 * in common code already does so.
 */
uint32_t crec_flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	if (access_disabled)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	if (is_wp_enabled())
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/* Check if blocks were stuck locked at pre-init */
	if (stuck_locked)
		flags |= EC_FLASH_PROTECT_ERROR_STUCK;

	return flags;
}

#define WP_RANGE(start, count) (((1 << (count)) - 1) << (start))
#define RO_WP_RANGE WP_RANGE(WP_BANK_OFFSET, WP_BANK_COUNT)

int crec_flash_physical_protect_now(int all)
{
	protect_blocks(RO_WP_RANGE);

	/*
	 * Lock the option bytes or the full access by writing a wrong
	 * key to FLASH_*KEYR. This triggers a bus fault, so we need to
	 * disable bus fault handler while doing this.
	 *
	 * This incorrect key fault causes the flash to become
	 * permanently locked until reset, a correct keyring write
	 * will not unlock it.
	 */

	if (all) {
		/* cannot do any write/erase access until next reboot */
		disable_flash_control_register();
		access_disabled = 1;
	}
	/* cannot modify the WP bits in the option bytes until reboot */
	disable_flash_option_bytes();
	option_disabled = 1;

	return EC_SUCCESS;
}

int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	int new_wp_enable = !!(new_flags & EC_FLASH_PROTECT_RO_AT_BOOT);

	if (is_wp_enabled() != new_wp_enable)
		return set_wp(new_wp_enable);

	return EC_SUCCESS;
}

uint32_t crec_flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t crec_flash_physical_get_writable_flags(uint32_t cur_flags)
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

int crec_flash_physical_restore_state(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	int version, size;
	const struct flash_wp_state *prev;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection. We simply need to represent these
	 * irreversible flags to other components.
	 */
	if (reset_flags & EC_RESET_FLAG_SYSJUMP) {
		prev = (const struct flash_wp_state *)system_get_jump_tag(
			FLASH_SYSJUMP_TAG, &version, &size);
		if (prev && version == FLASH_HOOK_VERSION &&
		    size == sizeof(*prev)) {
			access_disabled = prev->access_disabled;
			option_disabled = prev->option_disabled;
			stuck_locked = prev->stuck_locked;
		}
		return 1;
	}

	return 0;
}

int crec_flash_pre_init(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	uint32_t prot_flags = crec_flash_get_protect();
	uint32_t unwanted_prot_flags = EC_FLASH_PROTECT_ALL_NOW |
				       EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	if (crec_flash_physical_restore_state())
		return EC_SUCCESS;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection. Nothing additional needs to be done.
	 */
	if (reset_flags & EC_RESET_FLAG_SYSJUMP)
		return EC_SUCCESS;

	if (prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
		/*
		 * Write protect is asserted.  If we want RO flash protected,
		 * protect it now.
		 */
		if ((prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT) &&
		    !(prot_flags & EC_FLASH_PROTECT_RO_NOW)) {
			int rv;

			rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_NOW,
						    EC_FLASH_PROTECT_RO_NOW);
			if (rv)
				return rv;

			/* Re-read flags */
			prot_flags = crec_flash_get_protect();
		}
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
	if (reset_flags & EC_RESET_FLAG_POWER_ON) {
		stuck_locked = 1;
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Otherwise, do a hard boot to clear the flash protection registers */
	system_reset(SYSTEM_RESET_HARD | SYSTEM_RESET_PRESERVE_FLAGS);

	/* That doesn't return, so if we're still here that's an error */
	return EC_ERROR_UNKNOWN;
}

/*****************************************************************************/
/* Hooks */

static void flash_preserve_state(void)
{
	const struct flash_wp_state state = {
		.access_disabled = access_disabled,
		.option_disabled = option_disabled,
		.stuck_locked = stuck_locked,
	};

	system_add_jump_tag(FLASH_SYSJUMP_TAG, FLASH_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, flash_preserve_state, HOOK_PRIO_DEFAULT);
