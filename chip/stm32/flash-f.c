/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common flash memory module for STM32F and STM32F0 */

#include "battery.h"
#include "builtin/assert.h"
#include "clock.h"
#include "console.h"
#include "flash-f.h"
#include "flash.h"
#include "hooks.h"
#include "panic.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#include <stdbool.h>

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/*
 * Approximate number of CPU cycles per iteration of the loop when polling
 * the flash status
 */
#define CYCLE_PER_FLASH_LOOP 10

/*
 * While flash write / erase is in progress, the stm32 CPU core is mostly
 * non-functional, due to the inability to fetch instructions from flash.
 * This may greatly increase interrupt latency.
 */

/* Flash page programming timeout.  This is 2x the datasheet max. */
#define FLASH_WRITE_TIMEOUT_US 16000
/* 20ms < tERASE < 40ms on F0/F3, for 1K / 2K sector size. */
#define FLASH_ERASE_TIMEOUT_US 40000

#if defined(CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE)
#if !defined(CHIP_FAMILY_STM32F4)
#error "CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE should work with all STM32F "
"series chips, but has not been tested"
#endif /* !CHIP_FAMILY_STM32F4 */
#endif /* CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE */

/* Forward declarations */
#if defined(CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE)
	static enum flash_rdp_level
	flash_physical_get_rdp_level(void);
static int flash_physical_set_rdp_level(enum flash_rdp_level level);
#endif /* CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE */

static inline int calculate_flash_timeout(void)
{
	return (FLASH_WRITE_TIMEOUT_US * (clock_get_freq() / SECOND) /
		CYCLE_PER_FLASH_LOOP);
}

static int wait_busy(void)
{
	int timeout = calculate_flash_timeout();
	while ((STM32_FLASH_SR & FLASH_SR_BUSY) && timeout-- > 0)
		udelay(CYCLE_PER_FLASH_LOOP);
	return (timeout > 0) ? EC_SUCCESS : EC_ERROR_TIMEOUT;
}

void unlock_flash_control_register(void)
{
	STM32_FLASH_KEYR = FLASH_KEYR_KEY1;
	STM32_FLASH_KEYR = FLASH_KEYR_KEY2;
}

void unlock_flash_option_bytes(void)
{
	STM32_FLASH_OPTKEYR = FLASH_OPTKEYR_KEY1;
	STM32_FLASH_OPTKEYR = FLASH_OPTKEYR_KEY2;
}

void disable_flash_option_bytes(void)
{
	ignore_bus_fault(1);
	/*
	 * Writing anything other than the pre-defined keys to the option key
	 * register results in a bus fault and the register being locked until
	 * reboot (even with a further correct key write).
	 */
	STM32_FLASH_OPTKEYR = 0xffffffff;
	ignore_bus_fault(0);
}

void disable_flash_control_register(void)
{
	ignore_bus_fault(1);
	/*
	 * Writing anything other than the pre-defined keys to the key
	 * register results in a bus fault and the register being locked until
	 * reboot (even with a further correct key write).
	 */
	STM32_FLASH_KEYR = 0xffffffff;
	ignore_bus_fault(0);
}

void lock_flash_control_register(void)
{
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
	/* FLASH_CR_OPTWRE was set by writing the keys in unlock(). */
	STM32_FLASH_CR &= ~FLASH_CR_OPTWRE;
#endif
	STM32_FLASH_CR |= FLASH_CR_LOCK;
}

void lock_flash_option_bytes(void)
{
#if !(defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3))
	STM32_FLASH_OPTCR |= FLASH_OPTLOCK;
#endif
}

bool flash_option_bytes_locked(void)
{
	return !!STM32_FLASH_OPT_LOCKED;
}

bool flash_control_register_locked(void)
{
	return !!(STM32_FLASH_CR & FLASH_CR_LOCK);
}

/*
 * We at least unlock the control register lock.
 * We may also unlock other locks.
 */
enum extra_lock_type {
	NO_EXTRA_LOCK = 0,
	OPT_LOCK = 1,
};

static int unlock(int locks)
{
	/*
	 * We may have already locked the flash module and get a bus fault
	 * in the attempt to unlock. Need to disable bus fault handler now.
	 */
	ignore_bus_fault(1);

	/* Always unlock CR if needed */
	if (flash_control_register_locked())
		unlock_flash_control_register();

	/* unlock option memory if required */
	if ((locks & OPT_LOCK) && flash_option_bytes_locked())
		unlock_flash_option_bytes();

	/* Re-enable bus fault handler */
	ignore_bus_fault(0);

	if ((locks & OPT_LOCK) && flash_option_bytes_locked())
		return EC_ERROR_UNKNOWN;
	if (STM32_FLASH_CR & FLASH_CR_LOCK)
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}

static void lock(void)
{
	lock_flash_control_register();
}

#ifdef CHIP_FAMILY_STM32F4
static int write_optb(uint32_t mask, uint32_t value)
{
	int rv;

	rv = wait_busy();
	if (rv)
		return rv;

	/* The target byte is the value we want to write. */
	if ((STM32_FLASH_OPTCR & mask) == value)
		return EC_SUCCESS;

	rv = unlock(OPT_LOCK);
	if (rv)
		return rv;

	STM32_FLASH_OPTCR = (STM32_FLASH_OPTCR & ~mask) | value;
	STM32_FLASH_OPTCR |= FLASH_OPTSTRT;

	rv = wait_busy();
	if (rv)
		return rv;
	lock();

	return EC_SUCCESS;
}
#else
static int write_optb(int byte, uint8_t value);
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
	STM32_FLASH_CR |= FLASH_CR_OPTER;
	STM32_FLASH_CR |= FLASH_CR_STRT;

	rv = wait_busy();

	STM32_FLASH_CR &= ~FLASH_CR_OPTER;

	if (rv)
		return rv;
	lock();

	return EC_SUCCESS;
}

static int write_optb(int byte, uint8_t value);
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
	STM32_FLASH_CR |= FLASH_CR_OPTPG;

	*hword = ((~value) << STM32_OPTB_COMPL_SHIFT) | value;

	/* reset OPTPG bit */
	STM32_FLASH_CR &= ~FLASH_CR_OPTPG;

	rv = wait_busy();
	if (rv)
		return rv;
	lock();

	return EC_SUCCESS;
}
#endif

#if defined(CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE)
/**
 * @return true if RDP (read protection) Level 1 or 2 enabled, false otherwise
 */
bool is_flash_rdp_enabled(void)
{
	enum flash_rdp_level level = flash_physical_get_rdp_level();

	if (level == FLASH_RDP_LEVEL_INVALID) {
		CPRINTS("ERROR: unable to read RDP level");
		return false;
	}

	return level != FLASH_RDP_LEVEL_0;
}
#endif /* CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE */

/*****************************************************************************/
/* Physical layer APIs */

int crec_flash_physical_write(int offset, int size, const char *data)
{
#if CONFIG_FLASH_WRITE_SIZE == 1
	uint8_t *address = (uint8_t *)(CONFIG_PROGRAM_MEMORY_BASE + offset);
	uint8_t quantum = 0;
#elif CONFIG_FLASH_WRITE_SIZE == 2
	uint16_t *address = (uint16_t *)(CONFIG_PROGRAM_MEMORY_BASE + offset);
	uint16_t quantum = 0;
#elif CONFIG_FLASH_WRITE_SIZE == 4
	uint32_t *address = (uint32_t *)(CONFIG_PROGRAM_MEMORY_BASE + offset);
	uint32_t quantum = 0;
#else
#error "CONFIG_FLASH_WRITE_SIZE not supported."
#endif
	int res = EC_SUCCESS;
	int timeout = calculate_flash_timeout();

	if (unlock(NO_EXTRA_LOCK) != EC_SUCCESS) {
		res = EC_ERROR_UNKNOWN;
		goto exit_wr;
	}

	/* Clear previous error status */
	STM32_FLASH_SR = FLASH_SR_ALL_ERR | FLASH_SR_EOP;

	/* set PG bit */
	STM32_FLASH_CR |= FLASH_CR_PG;

	for (; size > 0; size -= CONFIG_FLASH_WRITE_SIZE) {
		int i;

		for (i = CONFIG_FLASH_WRITE_SIZE - 1, quantum = 0; i >= 0; i--)
			quantum = (quantum << 8) + data[i];
		data += CONFIG_FLASH_WRITE_SIZE;
		/*
		 * Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing with interrupt disabled.
		 */
		watchdog_reload();

		/* wait to be ready  */
		for (i = 0; (STM32_FLASH_SR & FLASH_SR_BUSY) && (i < timeout);
		     i++)
			;

		/* write the data */
		*address++ = quantum;

		/* Wait for writes to complete */
		for (i = 0; (STM32_FLASH_SR & FLASH_SR_BUSY) && (i < timeout);
		     i++)
			;

		if (STM32_FLASH_SR & FLASH_SR_BUSY) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (STM32_FLASH_SR & FLASH_SR_ALL_ERR) {
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
	int sector_size;
	int timeout_us;
#ifdef CHIP_FAMILY_STM32F4
	int sector = crec_flash_bank_index(offset);
	/* we take advantage of sector_size == erase_size */
	if ((sector < 0) || (crec_flash_bank_index(offset + size) < 0))
		return EC_ERROR_INVAL; /* Invalid range */
#endif

	if (unlock(NO_EXTRA_LOCK) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/* Clear previous error status */
	STM32_FLASH_SR = FLASH_SR_ALL_ERR | FLASH_SR_EOP;

	/* set SER/PER bit */
	STM32_FLASH_CR |= FLASH_CR_PER;

	while (size > 0) {
		timestamp_t deadline;
#ifdef CHIP_FAMILY_STM32F4
		sector_size = crec_flash_bank_size(sector);
		/* Timeout: from spec, proportional to the size
		 * inversely proportional to the write size.
		 */
		timeout_us = sector_size * 4 / CONFIG_FLASH_WRITE_SIZE;
#else
		sector_size = CONFIG_FLASH_ERASE_SIZE;
		timeout_us = FLASH_ERASE_TIMEOUT_US;
#endif
		/* Do nothing if already erased */
		if (crec_flash_is_erased(offset, sector_size))
			goto next_sector;
#ifdef CHIP_FAMILY_STM32F4
		/* select page to erase */
		STM32_FLASH_CR = (STM32_FLASH_CR & ~STM32_FLASH_CR_SNB_MASK) |
				 (sector << STM32_FLASH_CR_SNB_OFFSET);
#else
		/* select page to erase */
		STM32_FLASH_AR = CONFIG_PROGRAM_MEMORY_BASE + offset;
#endif
		/* set STRT bit : start erase */
		STM32_FLASH_CR |= FLASH_CR_STRT;

		deadline.val = get_time().val + timeout_us;
		/* Wait for erase to complete */
		watchdog_reload();
		while ((STM32_FLASH_SR & FLASH_SR_BUSY) &&
		       (get_time().val < deadline.val)) {
			crec_usleep(timeout_us / 100);
		}
		if (STM32_FLASH_SR & FLASH_SR_BUSY) {
			res = EC_ERROR_TIMEOUT;
			goto exit_er;
		}

		/*
		 * Check for error conditions - erase failed, voltage error,
		 * protection error
		 */
		if (STM32_FLASH_SR & FLASH_SR_ALL_ERR) {
			res = EC_ERROR_UNKNOWN;
			goto exit_er;
		}
	next_sector:
		size -= sector_size;
		offset += sector_size;
#ifdef CHIP_FAMILY_STM32F4
		sector++;
#endif
	}

exit_er:
	/* reset SER/PER bit */
	STM32_FLASH_CR &= ~FLASH_CR_PER;

	lock();

	return res;
}

#ifdef CHIP_FAMILY_STM32F4
static int flash_physical_get_protect_at_boot(int block)
{
	/* 0: Write protection active on sector i. */
	return !(STM32_OPTB_WP & STM32_OPTB_nWRP(block));
}

static int flash_physical_protect_at_boot_update_rdp_pstate(uint32_t new_flags)
{
#if defined(CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE)
	int rv = EC_SUCCESS;

	bool rdp_enable = (new_flags & EC_FLASH_PROTECT_RO_AT_BOOT) != 0;

	/*
	 * This is intentionally a one-way latch. Once we have enabled RDP
	 * Level 1, we will only allow going back to Level 0 using the
	 * bootloader (e.g., "stm32mon -U") since transitioning from Level 1 to
	 * Level 0 triggers a mass erase.
	 */
	if (rdp_enable)
		rv = flash_physical_set_rdp_level(FLASH_RDP_LEVEL_1);

	return rv;
#else
	return EC_SUCCESS;
#endif
}

int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	int block;
	int original_val, val;

	original_val = val = STM32_OPTB_WP & STM32_OPTB_nWRP_ALL;

	for (block = WP_BANK_OFFSET; block < WP_BANK_OFFSET + PHYSICAL_BANKS;
	     block++) {
		int protect = new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT;

		if (block >= WP_BANK_OFFSET &&
		    block < WP_BANK_OFFSET + WP_BANK_COUNT)
			protect |= new_flags & EC_FLASH_PROTECT_RO_AT_BOOT;
#ifdef CONFIG_FLASH_PROTECT_RW
		else
			protect |= new_flags & EC_FLASH_PROTECT_RW_AT_BOOT;
#endif

		if (protect)
			val &= ~BIT(block);
		else
			val |= 1 << block;
	}
	if (original_val != val) {
		int rv = write_optb(STM32_FLASH_nWRP_ALL,
				    val << STM32_FLASH_nWRP_OFFSET);
		if (rv != EC_SUCCESS)
			return rv;
	}

	return flash_physical_protect_at_boot_update_rdp_pstate(new_flags);
}

static void unprotect_all_blocks(void)
{
	write_optb(STM32_FLASH_nWRP_ALL, STM32_FLASH_nWRP_ALL);
}

#else /* CHIP_FAMILY_STM32F4 */
static int flash_physical_get_protect_at_boot(int block)
{
	uint8_t val = read_optb(STM32_OPTB_WRP_OFF(block / 8));
	return (!(val & (1 << (block % 8)))) ? 1 : 0;
}

int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	int block;
	int i;
	int original_val[4], val[4];

	for (i = 0; i < 4; ++i)
		original_val[i] = val[i] = read_optb(i * 2 + 8);

	for (block = WP_BANK_OFFSET; block < WP_BANK_OFFSET + PHYSICAL_BANKS;
	     block++) {
		int protect = new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT;
		int byte_off = STM32_OPTB_WRP_OFF(block / 8) / 2 - 4;

		if (block >= WP_BANK_OFFSET &&
		    block < WP_BANK_OFFSET + WP_BANK_COUNT)
			protect |= new_flags & EC_FLASH_PROTECT_RO_AT_BOOT;
#ifdef CONFIG_ROLLBACK
		else if (block >= ROLLBACK_BANK_OFFSET &&
			 block < ROLLBACK_BANK_OFFSET + ROLLBACK_BANK_COUNT)
			protect |= new_flags &
				   EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif
#ifdef CONFIG_FLASH_PROTECT_RW
		else
			protect |= new_flags & EC_FLASH_PROTECT_RW_AT_BOOT;
#endif

		if (protect)
			val[byte_off] = val[byte_off] & (~(1 << (block % 8)));
		else
			val[byte_off] = val[byte_off] | (1 << (block % 8));
	}

	for (i = 0; i < 4; ++i)
		if (original_val[i] != val[i])
			write_optb(i * 2 + 8, val[i]);

#ifdef CONFIG_FLASH_READOUT_PROTECTION
	/*
	 * Set a permanent protection by increasing RDP to level 1,
	 * trying to unprotected the flash will trigger a full erase.
	 */
	write_optb(0, 0x11);
#endif

	return EC_SUCCESS;
}

static void unprotect_all_blocks(void)
{
	int i;

	for (i = 4; i < 8; ++i)
		write_optb(i * 2, 0xff);
}
#endif

/**
 * Check if write protect register state is inconsistent with RO_AT_BOOT and
 * ALL_AT_BOOT state.
 *
 * @return zero if consistent, non-zero if inconsistent.
 */
static int registers_need_reset(void)
{
	uint32_t flags = crec_flash_get_protect();
	int i;
	int ro_at_boot = (flags & EC_FLASH_PROTECT_RO_AT_BOOT) ? 1 : 0;
	int ro_wp_region_start = WP_BANK_OFFSET;
	int ro_wp_region_end = WP_BANK_OFFSET + WP_BANK_COUNT;

	for (i = ro_wp_region_start; i < ro_wp_region_end; i++)
		if (flash_physical_get_protect_at_boot(i) != ro_at_boot)
			return 1;
	return 0;
}

#if defined(CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE)
/**
 * Set Flash RDP (read protection) level.
 *
 * @note Does not take effect until reset.
 *
 * @param level new RDP (read protection) level to set
 * @return EC_SUCCESS on success, other on failure
 */
int flash_physical_set_rdp_level(enum flash_rdp_level level)
{
	uint32_t reg_level;

	switch (level) {
	case FLASH_RDP_LEVEL_0:
		/*
		 * Asserting by default since we don't want to inadvertently
		 * go from Level 1 to Level 0, which triggers a mass erase.
		 * Remove assert if you want to use it.
		 */
		ASSERT(false);
		reg_level = FLASH_OPTCR_RDP_LEVEL_0;
		break;
	case FLASH_RDP_LEVEL_1:
		reg_level = FLASH_OPTCR_RDP_LEVEL_1;
		break;
	case FLASH_RDP_LEVEL_2:
		/*
		 * Asserting by default since it's permanent (there is no
		 * way to reverse). Remove assert if you want to use it.
		 */
		ASSERT(false);
		reg_level = FLASH_OPTCR_RDP_LEVEL_2;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return write_optb(FLASH_OPTCR_RDP_MASK, reg_level);
}

/**
 * @return On success, current flash read protection level.
 *         On failure, FLASH_RDP_LEVEL_INVALID
 */
enum flash_rdp_level flash_physical_get_rdp_level(void)
{
	uint32_t level = (STM32_FLASH_OPTCR & FLASH_OPTCR_RDP_MASK);

	switch (level) {
	case FLASH_OPTCR_RDP_LEVEL_0:
		return FLASH_RDP_LEVEL_0;
	case FLASH_OPTCR_RDP_LEVEL_1:
		return FLASH_RDP_LEVEL_1;
	case FLASH_OPTCR_RDP_LEVEL_2:
		return FLASH_RDP_LEVEL_2;
	default:
		return FLASH_RDP_LEVEL_INVALID;
	}
}
#endif /* CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE */

/*****************************************************************************/
/* High-level APIs */

int crec_flash_pre_init(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	uint32_t prot_flags = crec_flash_get_protect();
	int need_reset = 0;

#ifdef CHIP_FAMILY_STM32F4
	unlock(NO_EXTRA_LOCK);
	/* Set the proper write size */
	STM32_FLASH_CR = (STM32_FLASH_CR & ~STM32_FLASH_CR_PSIZE_MASK) |
			 (31 - __builtin_clz(CONFIG_FLASH_WRITE_SIZE))
				 << STM32_FLASH_CR_PSIZE_OFFSET;
	lock();
#endif
	if (crec_flash_physical_restore_state())
		return EC_SUCCESS;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection. Nothing additional needs to be done.
	 */
	if (reset_flags & EC_RESET_FLAG_SYSJUMP)
		return EC_SUCCESS;

	if (prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
		if (prot_flags & EC_FLASH_PROTECT_RO_NOW) {
			/* Enable physical protection for RO (0 means RO). */
			crec_flash_physical_protect_now(0);
		}

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
