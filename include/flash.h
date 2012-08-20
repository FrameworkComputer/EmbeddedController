/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#ifndef __CROS_EC_FLASH_H
#define __CROS_EC_FLASH_H

#include "common.h"
#include "config.h"
#include "ec_commands.h"  /* For EC_FLASH_PROTECT_* flags */

/*****************************************************************************/
/* Low-level methods, for use by flash_common. */

/**
 * Get the physical memory address of a flash offset
 *
 * This is used for direct flash access. We assume that the flash is
 * contiguous from this start address through to the end of the usable
 * flash.
 *
 * @param offset	Flash offset to get address of
 * @param dataptrp	Returns pointer to memory address of flash offset
 * @return pointer to flash memory offset, if ok, else NULL
 */
static inline char *flash_physical_dataptr(int offset)
{
	return (char *)(CONFIG_FLASH_BASE + offset);
}

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
int flash_dataptr(int offset, int size_req, int align, char **ptrp);

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
