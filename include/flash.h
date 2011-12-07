/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#ifndef __CROS_EC_FLASH_H
#define __CROS_EC_FLASH_H

#include "common.h"


#define FLASH_WRITE_BYTES      4
#define FLASH_FWB_WORDS       32
#define FLASH_FWB_BYTES (FLASH_FWB_WORDS * 4)
#define FLASH_ERASE_BYTES   1024
#define FLASH_PROTECT_BYTES 2048


/* Initializes the module. */
int flash_init(void);

/* Returns the usable size of flash in bytes.  Note that this is
 * smaller than the actual flash size, */
int flash_get_size(void);

/* Returns the write / erase / protect block size, in bytes.
 * Operations must be aligned to and multiples of the granularity.
 * For example, erase operations must have offset and size which are
 * multiples of the erase block size. */
int flash_get_write_block_size(void);
int flash_get_erase_block_size(void);
int flash_get_protect_block_size(void);

/* Reads <size> bytes of data from offset <offset> into <data>. */
int flash_read(int offset, int size, char *data);

/* Writes <size> bytes of data to flash at byte offset <offset>.
 * <data> must be 32-bit aligned. */
int flash_write(int offset, int size, const char *data);

/* Erases <size> bytes of flash at byte offset <offset>. */
int flash_erase(int offset, int size);

/* TODO: not super happy about the following APIs yet.
 *
 * The theory of operation is that we'll use the last page of flash to
 * hold the write protect range, and the flag for whether the last
 * page itself should be protected.  Then when flash_init() is called,
 * it checks if the write protect pin is asserted, and if so, it
 * writes (but does not commit) the flash protection registers.
 *
 * This simulates what a SPI flash does, where the status register
 * holds the write protect range, and a bit which protects the status
 * register itself.  The bit is only obeyed if the write protect pin
 * is enabled.
 *
 * It's an imperfect simulation, because in a SPI flash, as soon as
 * you deassert the pin you can alter the status register, where here
 * it'll take a cold boot to clear protection.  Also, here protection
 * gets written to the registers as soon as you set the write protect
 * lock, which is different than SPI, where it's effective as soon as
 * you set the write protect range. */

/* Gets or sets the write protect range in bytes.  This setting is
 * stored in flash, and persists across reboots.  If size is non-zero,
 * the write protect range is also locked, and may not be subsequently
 * altered until after a cold boot with the write protect pin
 * deasserted. */
int flash_get_write_protect_range(int *offset, int *size);
int flash_set_write_protect_range(int offset, int size);

/* The write protect range has been stored into the chip registers
 * this boot.  The flash is write protected and the range cannot be
 * changed without rebooting. */
#define EC_FLASH_WP_RANGE_LOCKED         0x01
/* The write protect pin was asserted at init time. */
#define EC_FLASH_WP_PIN_ASSERTED_AT_INIT 0x02
/* The write protect pin is asserted now. */
#define EC_FLASH_WP_PIN_ASSERTED_NOW     0x04

/* Returns the current write protect status; see EC_FLASH_WP_*
 * for valid flags. */
int flash_get_write_protect_status(void);


#endif  /* __CROS_EC_FLASH_H */
