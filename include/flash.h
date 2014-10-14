/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#ifndef __CROS_EC_FLASH_H
#define __CROS_EC_FLASH_H

#include "common.h"
#include "ec_commands.h"  /* For EC_FLASH_PROTECT_* flags */

/* Number of physical flash banks */
#define PHYSICAL_BANKS (CONFIG_FLASH_PHYSICAL_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Read-only firmware offset and size in units of flash banks */
#define RO_BANK_OFFSET		(CONFIG_FW_RO_OFF / CONFIG_FLASH_BANK_SIZE)
#define RO_BANK_COUNT		(CONFIG_FW_RO_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Read-write firmware offset and size in units of flash banks */
#define RW_BANK_OFFSET		(CONFIG_FW_RW_OFF / CONFIG_FLASH_BANK_SIZE)
#define RW_BANK_COUNT		(CONFIG_FW_RW_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Persistent protection state flash offset / size / bank */
#define PSTATE_OFFSET		CONFIG_FW_PSTATE_OFF
#define PSTATE_SIZE		CONFIG_FW_PSTATE_SIZE
#define PSTATE_BANK		(PSTATE_OFFSET / CONFIG_FLASH_BANK_SIZE)
#define PSTATE_BANK_COUNT	(PSTATE_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Range of write protection */
enum flash_wp_range {
	FLASH_WP_NONE = 0,
	FLASH_WP_RO,
	FLASH_WP_ALL,
};

/*****************************************************************************/
/* Low-level methods, for use by flash_common. */

/**
 * Write to physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE.
 *
 * @param offset	Flash offset to write.
 * @param size	        Number of bytes to write.
 * @param data          Data to write to flash.  Must be 32-bit aligned.
 */
int flash_physical_write(int offset, int size, const char *data);

/**
 * Erase physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE.
 *
 * @param offset	Flash offset to erase.
 * @param size	        Number of bytes to erase.
 */
int flash_physical_erase(int offset, int size);

/**
 * Read physical write protect setting for a flash bank.
 *
 * @param bank	        Bank index to check.
 * @return non-zero if bank is protected until reboot.
 */
int flash_physical_get_protect(int bank);

/**
 * Return flash protect state flags from the physical layer.
 *
 * This should only be called by flash_get_protect().
 *
 * Uses the EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t flash_physical_get_protect_flags(void);

/**
 * Enable/disable protecting firmware/pstate at boot.
 *
 * @param range		The range to protect
 * @return non-zero if error.
 */
int flash_physical_protect_at_boot(enum flash_wp_range range);

/**
 * Protect flash now.
 *
 * @param all		Protect all (=1) or just read-only and pstate (=0).
 * @return non-zero if error.
 */
int flash_physical_protect_now(int all);

/**
 * Force reload of flash protection bits.
 *
 * Some EC architectures (STM32L) only load the bits from option bytes at
 * power-on reset or via a special command.  This issues that command if
 * possible, which triggers a power-on reboot.
 *
 * Only returns (with EC_ERROR_ACCESS_DENIED) if the command is locked.
 */
int flash_physical_force_reload(void);

/**
 * Restore flash physical layer state after sysjump.
 *
 * @return non-zero if restored.
 */
int flash_physical_restore_state(void);

/**
 * Return the valid flash protect flags.
 *
 * @return a combination of EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t flash_physical_get_valid_flags(void);

/**
 * Return the writable flash protect flags.
 *
 * @param cur_flags The current flash protect flags.
 * @return a combination of EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t flash_physical_get_writable_flags(uint32_t cur_flags);

/*****************************************************************************/
/* Low-level common code for use by flash modules. */

/**
 * Check if a region of flash is erased
 *
 * It is assumed that an erased region has all bits set to 1.
 *
 * @param offset	Flash offset to check
 * @param size		Number of bytes to check (word-aligned)
 * @return 1 if erased, 0 if not erased
 */
int flash_is_erased(uint32_t offset, int size);

/**
 * Enable write protect for the specified range.
 *
 * Once write protect is enabled, it will STAY enabled until the system is
 * hard-rebooted with the hardware write protect pin deasserted.  If the write
 * protect pin is deasserted, the protect setting is ignored, and the entire
 * flash will be writable.
 *
 * @param range		The range to protect.
 * @return EC_SUCCESS, or nonzero if error.
 */
int flash_protect_at_boot(enum flash_wp_range range);

/*****************************************************************************/
/* High-level interface for use by other modules. */

/**
 * Initialize the module.
 *
 * Applies at-boot protection settings if necessary.
 */
int flash_pre_init(void);

/**
 * Return the usable size of flash in bytes.  Note that this may be smaller
 * than the physical flash size.
 */
int flash_get_size(void);

/**
 * Get the physical memory address of a flash offset
 *
 * This is used for direct flash access. We assume that the flash is
 * contiguous from this start address through to the end of the usable
 * flash.
 *
 * This function returns -1 if offset + size_req extends beyond the end
 * of flash, the offset is out of range, or if either size_req or offset
 * are not aligned to 'align'.
 *
 * @param offset	Flash offset to get address of
 * @param size_req	Number of bytes requested
 * @param align		Ensure offset and size_req are aligned to given
 *			power of two.
 * @param ptrp		If not NULL, returns a pointer to this flash offset
 *			in memory, unless function fails, iwc it is unset.
 * @return size of flash region available at *ptrp, or -1 on error
 */
int flash_dataptr(int offset, int size_req, int align, const char **ptrp);

/**
 * Write to flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE.
 *
 * @param offset	Flash offset to write.
 * @param size	        Number of bytes to write.
 * @param data          Data to write to flash.  Must be 32-bit aligned.
 */
int flash_write(int offset, int size, const char *data);

/**
 * Erase flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE.
 *
 * @param offset	Flash offset to erase.
 * @param size	        Number of bytes to erase.
 */
int flash_erase(int offset, int size);

/**
 * Return the flash protect state.
 *
 * Uses the EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t flash_get_protect(void);

/**
 * Set the flash protect state.
 *
 * @param mask		Bits in flags to apply.
 * @param flags		New values for flags.
 */
int flash_set_protect(uint32_t mask, uint32_t flags);

#endif  /* __CROS_EC_FLASH_H */
