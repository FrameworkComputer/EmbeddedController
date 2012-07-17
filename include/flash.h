/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#ifndef __CROS_EC_FLASH_H
#define __CROS_EC_FLASH_H

#include "common.h"


/*****************************************************************************/
/* Low-level methods, for use by flash_common. */

/**
 * Initialize the physical flash interface.
 */
int flash_physical_pre_init(void);

/* Return the write / erase / protect block size, in bytes.  Operations must be
 * aligned to and multiples of the granularity.  For example, erase operations
 * must have offset and size which are multiples of the erase block size. */
int flash_get_write_block_size(void);
int flash_get_erase_block_size(void);
int flash_get_protect_block_size(void);

/* Return the physical size of flash in bytes */
int flash_physical_size(void);

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
	return (char *)offset;
}

/* Read <size> bytes of data from offset <offset> into <data>. */
int flash_physical_read(int offset, int size, char *data);

/* Write <size> bytes of data to flash at byte offset <offset>.
 * <data> must be 32-bit aligned. */
int flash_physical_write(int offset, int size, const char *data);

/* Erase <size> bytes of flash at byte offset <offset>. */
int flash_physical_erase(int offset, int size);

/* Return non-zero if bank is protected until reboot. */
int flash_physical_get_protect(int bank);

/**
 * Protect the flash banks until reboot.
 *
 * @param start_bank    Start bank to protect
 * @param bank_count    Number of banks to protect
 */
void flash_physical_set_protect(int start_bank, int bank_count);

/*****************************************************************************/
/* High-level interface for use by other modules. */

/* Initializes the module. */
int flash_pre_init(void);

/* Returns the usable size of flash in bytes.  Note that this is
 * smaller than the actual flash size, */
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

/* Reads <size> bytes of data from offset <offset> into <data>. */
int flash_read(int offset, int size, char *data);

/* Writes <size> bytes of data to flash at byte offset <offset>.
 * <data> must be 32-bit aligned. */
int flash_write(int offset, int size, const char *data);

/* Erases <size> bytes of flash at byte offset <offset>. */
int flash_erase(int offset, int size);

/* Flash protection APIs
 *
 * Flash can be protected on a per-block basis at any point by calling
 * flash_protect_until_reboot().  Once a block is protected, it will stay
 * protected until reboot.  This function may be called at any time, regardless
 * of the persistent flash protection state, and protection will be applied
 * immediately.
 *
 * Flash may also be protected in a persistent fashion by calling
 * flash_set_protect().  This sets a persistent flag for each block which is
 * checked at boot time and applied if the hardware write protect pin is
 * enabled.
 *
 * The flash persistent protection settings are themselves protected by a lock,
 * which can be set via flash_lock_protect().  Once the protection settings are
 * locked:
 *
 *   (1) They will be immediately applied (as if flash_protect_until_reboot()
 *   had been called).
 *
 *   (2) The persistent settings cannot be changed.  That is, subsequent calls
 *   to flash_set_protect() and flash_lock_protect() will fail.
 *
 * The lock can be bypassed by cold-booting the system with the hardware write
 * protect pin deasserted.  In this case, the persistent settings and lock
 * state may be changed until flash_lock_protect(non-zero) is called, or until
 * the system is rebooted with the write protect pin asserted - at which point,
 * protection is re-applied. */

/**
 * Protect the entire flash until reboot.
 */
int flash_protect_until_reboot(void);

/* Higher-level APIs to emulate SPI write protect */

/**
 * Enable write protect for the read-only code.
 *
 * Once write protect is enabled, it will STAY enabled until the system is
 * hard-rebooted with the hardware write protect pin deasserted.  If the write
 * protect pin is deasserted, the protect setting is ignored, and the entire
 * flash will be writable.
 *
 * @param enable        Enable write protection
 */
int flash_enable_protect(int enable);

/* Flags for flash_get_protect_lock() */
/*
 * Flash protection lock has been set.  Note that if the write protect pin was
 * deasserted at boot time, this simply indicates the state of the lock
 * setting, and not whether blocks are actually protected.
 */
#define FLASH_PROTECT_RO_AT_BOOT   (1 << 0)
/*
 * Flash protection lock has actually been applied. Read-only firmware is
 * protected, and flash protection cannot be unlocked.
 */
#define FLASH_PROTECT_RO_NOW       (1 << 1)
/* Write protect pin is currently asserted */
#define FLASH_PROTECT_PIN_ASSERTED (1 << 2)
/* Entire flash is protected until reboot */
#define FLASH_PROTECT_RW_NOW       (1 << 3)
/* At least one bank of flash is stuck locked, and cannot be unlocked */
#define FLASH_PROTECT_STUCK_LOCKED (1 << 4)
/* At least one bank of flash which should be protected is not protected */
#define FLASH_PROTECT_PARTIAL      (1 << 5)

/* Return the flash protect lock status. */
int flash_get_protect(void);

#endif  /* __CROS_EC_FLASH_H */
