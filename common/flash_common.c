/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC - common functions */

#include "config.h"
#include "flash.h"
#include "gpio.h"
#include "registers.h"
#include "uart.h"
#include "util.h"

#define PERSIST_STATE_VERSION 1
#define MAX_BANKS (CONFIG_FLASH_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Persistent protection state */
struct persist_state {
	uint8_t version;            /* Version of this struct */
	uint8_t lock;               /* Lock flags */
	uint8_t reserved[2];        /* Reserved; set 0 */
	uint8_t blocks[MAX_BANKS];  /* Per-block flags */
};

static int usable_flash_size;       /* Usable flash size, not counting pstate */
static struct persist_state pstate; /* RAM copy of pstate data */


/* Return non-zero if the write protect pin is asserted */
static int wp_pin_asserted(void)
{
	return gpio_get_level(GPIO_WRITE_PROTECT);
}


/* Read persistent state into pstate. */
static int read_pstate(void)
{
	int i;
	int rv = flash_physical_read(usable_flash_size, sizeof(pstate),
				     (char *)&pstate);
	if (rv)
		return rv;

	/* Sanity-check data and initialize if necessary */
	if (pstate.version != PERSIST_STATE_VERSION) {
		memset(&pstate, 0, sizeof(pstate));
		pstate.version = PERSIST_STATE_VERSION;
	}

	/* Mask off currently-valid flags */
	pstate.lock &= FLASH_PROTECT_LOCK_SET;
	for (i = 0; i < MAX_BANKS; i++)
		pstate.blocks[i] &= FLASH_PROTECT_PERSISTENT;

	return EC_SUCCESS;
}


/* Write persistent state from pstate, erasing if necessary. */
static int write_pstate(void)
{
	int rv;

	/* Erase top protection block.  Assumes pstate size is less than
	 * erase/protect block size, and protect block size is less than erase
	 * block size. */
	/* TODO: optimize based on current physical flash contents; we can
	 * avoid the erase if we're only changing 1's into 0's. */
	rv = flash_physical_erase(usable_flash_size,
				  flash_get_protect_block_size());
	if (rv)
		return rv;

	/* Note that if we lose power in here, we'll lose the pstate contents.
	 * That's ok, because it's only possible to write the pstate before
	 * it's protected. */

	/* Rewrite the data */
	return flash_physical_write(usable_flash_size, sizeof(pstate),
				    (const char *)&pstate);
}


/* Apply write protect based on persistent state. */
static int apply_pstate(void)
{
	int pbsize = flash_get_protect_block_size();
	int banks = usable_flash_size / pbsize;
	int rv, i;

	/* If write protect is disabled, nothing to do */
	if (!wp_pin_asserted())
		return EC_SUCCESS;

	/* Read the current persist state from flash */
	rv = read_pstate();
	if (rv)
		return rv;

	/* If flash isn't locked, nothing to do */
	if (!(pstate.lock & FLASH_PROTECT_LOCK_SET))
		return EC_SUCCESS;

	/* Lock the protection data first */
	flash_physical_set_protect(banks);

	/* Then lock any banks necessary */
	for (i = 0; i < banks; i++) {
		if (pstate.blocks[i] & FLASH_PROTECT_PERSISTENT)
			flash_physical_set_protect(i);
	}

	return EC_SUCCESS;
}


/* Return non-zero if pstate block is already write-protected. */
static int is_pstate_lock_applied(void)
{
	int pstate_block = usable_flash_size / flash_get_protect_block_size();

	/* Fail if write protect block is already locked */
	return flash_physical_get_protect(pstate_block);
}


int flash_get_size(void)
{
	return usable_flash_size;
}


int flash_read(int offset, int size, char *data)
{
	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size)
		return EC_ERROR_UNKNOWN;  /* Invalid range */

	return flash_physical_read(offset, size, data);
}


int flash_write(int offset, int size, const char *data)
{
	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size ||
	    (offset | size) & (flash_get_write_block_size() - 1))
		return EC_ERROR_UNKNOWN;  /* Invalid range */

	/* TODO (crosbug.com/p/7478) - safety check - don't allow writing to
	 * the image we're running from */

	return flash_physical_write(offset, size, data);
}


int flash_erase(int offset, int size)
{
	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size ||
	    (offset | size) & (flash_get_erase_block_size() - 1))
		return EC_ERROR_UNKNOWN;  /* Invalid range */

	/* TODO (crosbug.com/p/7478) - safety check - don't allow erasing the
	 * image we're running from */

	return flash_physical_erase(offset, size);
}


int flash_protect_until_reboot(int offset, int size)
{
	int pbsize = flash_get_protect_block_size();
	int i;

	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size ||
	    (offset | size) & (pbsize - 1))
		return EC_ERROR_INVAL;  /* Invalid range */

	/* Convert offset and size to blocks */
	offset /= pbsize;
	size /= pbsize;

	for (i = 0; i < size; i++)
		flash_physical_set_protect(offset + i);
	return EC_SUCCESS;
}


int flash_set_protect(int offset, int size, int enable)
{
	uint8_t newflag = enable ? FLASH_PROTECT_PERSISTENT : 0;
	int pbsize = flash_get_protect_block_size();
	int rv, i;

	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size ||
	    (offset | size) & (pbsize - 1))
		return EC_ERROR_INVAL;  /* Invalid range */

	/* Fail if write protect block is already locked */
	if (is_pstate_lock_applied())
		return EC_ERROR_UNKNOWN;

	/* Read the current persist state from flash */
	rv = read_pstate();
	if (rv)
		return rv;

	/* Convert offset and size to blocks */
	offset /= pbsize;
	size /= pbsize;

	/* Set the new state */
	for (i = 0; i < size; i++) {
		pstate.blocks[offset + i] &= ~FLASH_PROTECT_PERSISTENT;
		pstate.blocks[offset + i] |= newflag;
	}

	/* Write the state back to flash */
	return write_pstate();
}


int flash_lock_protect(int lock)
{
	int rv;

	/* Fail if write protect block is already locked */
	if (is_pstate_lock_applied())
		return EC_ERROR_UNKNOWN;

	/* Read the current persist state from flash */
	rv = read_pstate();
	if (rv)
		return rv;

	/* Set the new flag */
	pstate.lock = lock ? FLASH_PROTECT_LOCK_SET : 0;

	/* Write the state back to flash */
	rv = write_pstate();
	if (rv)
		return rv;

	/* If unlocking, done now */
	if (!lock)
		return EC_SUCCESS;

	/* Otherwise, we need to apply all locks NOW */
	return apply_pstate();
}


const uint8_t *flash_get_protect_array(void)
{
	/* Return a copy of the current write protect state.  This is an array
	 * of per-protect-block flags.  (This is NOT the actual array, so
	 * attempting to change it will have no effect.) */
	int pbsize = flash_get_protect_block_size();
	int banks = usable_flash_size / pbsize;
	int i;

	/* Read the current persist state from flash */
	read_pstate();

	/* Combine with current block protection state */
	for (i = 0; i < banks; i++) {
		if (flash_physical_get_protect(i))
			pstate.blocks[i] |= FLASH_PROTECT_UNTIL_REBOOT;
	}

	/* Return the block array */
	return pstate.blocks;
}


int flash_get_protect(int offset, int size)
{
	int pbsize = flash_get_protect_block_size();
	uint8_t minflags = 0xff;
	int i;

	if (size < 0 || offset > usable_flash_size ||
	    offset + size > usable_flash_size ||
	    (offset | size) & (pbsize - 1))
		return 0;  /* Invalid range; assume nothing protected */

	/* Convert offset and size to blocks */
	offset /= pbsize;
	size /= pbsize;

	/* Read the current persist state from flash */
	read_pstate();

	/* Combine with current block protection state */
	for (i = 0; i < size; i++) {
		int f = pstate.blocks[offset + i];
		if (flash_physical_get_protect(offset + i))
			f |= FLASH_PROTECT_UNTIL_REBOOT;
		minflags &= f;
	}

	return minflags;
}


int flash_get_protect_lock(void)
{
	int flags;

	/* Read the current persist state from flash */
	read_pstate();
	flags = pstate.lock;

	/* Check if lock has been applied */
	if (is_pstate_lock_applied())
		flags |= FLASH_PROTECT_LOCK_APPLIED;

	/* Check if write protect pin is asserted now */
	if (wp_pin_asserted())
		flags |= FLASH_PROTECT_PIN_ASSERTED;

	return flags;
}

/*****************************************************************************/
/* Initialization */

int flash_pre_init(void)
{
	/* Calculate usable flash size.  Reserve one protection block
	 * at the top to hold the write protect data. */
	usable_flash_size = flash_physical_size() -
		flash_get_protect_block_size();

	/* Apply write protect to blocks if needed */
	return apply_pstate();
}
