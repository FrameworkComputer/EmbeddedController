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

/* Return non-zero if block is protected until reboot. */
int flash_physical_get_protect(int block);

/* Protects the block until reboot. */
void flash_physical_set_protect(int block);

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

/* Write-protect <size> bytes of flash at byte offset <offset> until next
 * reboot. */
int flash_protect_until_reboot(int offset, int size);

/* Higher-level APIs to emulate SPI write protect */

/* Set (enable=1) or clear (enable=0) the persistent write protect setting for
 * the specified range.  This will only succeed if write protect is unlocked.
 * This will take effect on the next boot, or when flash_lock_protect(1) is
 * called. */
int flash_set_protect(int offset, int size, int enable);

/* Lock or unlock the persistent write protect settings.  Once the write
 * protect settings are locked, they will STAY locked until the system is
 * cold-booted with the hardware write protect pin disabled.
 *
 * If called with lock!=0, this will also immediately protect all
 * persistently-protected blocks. */
int flash_lock_protect(int lock);

/* Flags for flash_get_protect() and flash_get_protect_array(). */
/* Protected persistently.  Note that if the write protect pin was deasserted
 * at boot time, a block may have the FLASH_PROTECT_PERSISTENT flag indicating
 * the block would be protected on a normal boot, but may not have the
 * FLASH_PROTECT_UNTIL_REBOOT flag indicating it's actually protected right
 * now. */
#define FLASH_PROTECT_PERSISTENT   0x01
/* Protected until reboot.  This will be set for persistently-protected blocks
 * as soon as the flash module protects them, and for non-persistent protection
 * after flash_protect_until_reboot() is called on a block. */
#define FLASH_PROTECT_UNTIL_REBOOT 0x02

/* Return a copy of the current write protect state.  This is an array of
 * per-protect-block flags.  The data is valid until the next call to a flash
 * function. */
const uint8_t *flash_get_protect_array(void);

/* Return the lowest amount of protection for any flash block in the specified
 * range.  That is, if any byte in the range is not protected until reboot,
 * FLASH_PROTECT_UNTIL_REBOOT will not be set. */
int flash_get_protect(int offset, int size);

/* Flags for flash_get_protect_lock() */
/* Flash protection lock has been set.  Note that if the write protect pin was
 * deasserted at boot time, this simply indicates the state of the lock
 * setting, and not whether blocks are actually protected. */
#define FLASH_PROTECT_LOCK_SET     0x01
/* Flash protection lock has actually been applied.  All blocks with
   FLASH_PROTECT_PERSISTENT have been protected, and flash protection cannot be
   unlocked. */
#define FLASH_PROTECT_LOCK_APPLIED 0x02
/* Write protect pin is currently asserted */
#define FLASH_PROTECT_PIN_ASSERTED 0x04

/* Return the flash protect lock status. */
int flash_get_protect_lock(void);

#endif  /* __CROS_EC_FLASH_H */
