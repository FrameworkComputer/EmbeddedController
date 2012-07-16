/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "flash.h"
#include "registers.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#define FLASH_WRITE_BYTES      4
#define FLASH_FWB_WORDS       32
#define FLASH_FWB_BYTES (FLASH_FWB_WORDS * 4)
#define FLASH_ERASE_BYTES   1024
#define FLASH_PROTECT_BYTES 2048

#define BANK_SHIFT 5 /* bank registers have 32bits each, 2^32 */
#define BANK_MASK ((1 << BANK_SHIFT) - 1) /* 5 bits */
#define F_BANK(b) ((b) >> BANK_SHIFT)
#define F_BIT(b) (1 << ((b) & BANK_MASK))

/* Flash timeouts.  These are 2x the spec sheet max. */
#define ERASE_TIMEOUT_MS 200
#define WRITE_TIMEOUT_US 300

int flash_get_write_block_size(void)
{
	return FLASH_WRITE_BYTES;
}


int flash_get_erase_block_size(void)
{
	return FLASH_ERASE_BYTES;
}


int flash_get_protect_block_size(void)
{
	BUILD_ASSERT(FLASH_PROTECT_BYTES == CONFIG_FLASH_BANK_SIZE);
	return FLASH_PROTECT_BYTES;
}


int flash_physical_size(void)
{
	return (LM4_FLASH_FSIZE + 1) * FLASH_PROTECT_BYTES;
}


int flash_physical_read(int offset, int size, char *data)
{
	/* Just read the flash from its memory window. */
	/* TODO: (crosbug.com/p/7473) is this affected by data cache?
	 * That is, if we read a block, then alter it, then read it
	 * again, do we get the old data? */
	memcpy(data, (char *)offset, size);
	return EC_SUCCESS;
}


/* Perform a write-buffer operation.  Buffer (FWB) and address (FMA) must be
 * pre-loaded. */
static int write_buffer(void)
{
	int t;

	if (!LM4_FLASH_FWBVAL)
		return EC_SUCCESS;  /* Nothing to do */

	/* Clear previous error status */
	LM4_FLASH_FCMISC = LM4_FLASH_FCRIS;

	/* Start write operation at page boundary */
	LM4_FLASH_FMC2 = 0xa4420001;

#ifdef CONFIG_TASK_WATCHDOG
	/* Reload the watchdog timer, so that writing a large amount of flash
	 * doesn't cause a watchdog reset. */
	watchdog_reload();
#endif

	/* Wait for write to complete */
	for (t = 0; LM4_FLASH_FMC2 & 0x01; t += 10) {
		if (t > WRITE_TIMEOUT_US)
			return EC_ERROR_TIMEOUT;
		udelay(10);
	}

	/* Check for error conditions - program failed, erase needed,
	 * voltage error. */
	if (LM4_FLASH_FCRIS & 0x2e01)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}


int flash_physical_write(int offset, int size, const char *data)
{
	const uint32_t *data32 = (const uint32_t *)data;
	int rv;
	int i;

	/* Get initial write buffer index and page */
	LM4_FLASH_FMA = offset & ~(FLASH_FWB_BYTES - 1);
	i = (offset >> 2) & (FLASH_FWB_WORDS - 1);

	/* Copy words into buffer */
	for ( ; size > 0; size -= 4) {
		LM4_FLASH_FWB[i++] = *data32++;
		if (i == FLASH_FWB_WORDS) {
			rv = write_buffer();
			if (rv != EC_SUCCESS)
				return rv;

			/* Advance to next page */
			i = 0;
			LM4_FLASH_FMA += FLASH_FWB_BYTES;
		}
	}

	/* Handle final partial page, if any */
	if (i > 0) {
		rv = write_buffer();
		if (rv != EC_SUCCESS)
			return rv;
	}
	return EC_SUCCESS;
}


int flash_physical_erase(int offset, int size)
{
	LM4_FLASH_FCMISC = LM4_FLASH_FCRIS;  /* Clear previous error status */
	LM4_FLASH_FMA = offset;

	for ( ; size > 0; size -= FLASH_ERASE_BYTES) {
		int t;

#ifdef CONFIG_TASK_WATCHDOG
		/* Reload the watchdog timer, so that erasing many flash pages
		 * doesn't cause a watchdog reset.  May not need this now that
		 * we're using usleep() below. */
		watchdog_reload();
#endif

		/* Start erase */
		LM4_FLASH_FMC = 0xa4420002;

		/* Wait for erase to complete */
		for (t = 0; LM4_FLASH_FMC & 0x02; t++) {
			if (t > ERASE_TIMEOUT_MS)
				return EC_ERROR_TIMEOUT;
			usleep(1000);
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (LM4_FLASH_FCRIS & 0x0a01)
			return EC_ERROR_UNKNOWN;

		LM4_FLASH_FMA += FLASH_ERASE_BYTES;
	}

	return EC_SUCCESS;
}


int flash_physical_get_protect(int block)
{
	return (LM4_FLASH_FMPPE[F_BANK(block)] & F_BIT(block)) ? 0 : 1;
}


void flash_physical_set_protect(int block)
{
	LM4_FLASH_FMPPE[F_BANK(block)] &= ~F_BIT(block);
}

int flash_physical_pre_init(void)
{
	int reset_flags = system_get_reset_flags();
	int any_wp = 0;
	int i;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection.  Nothing additional needs to be done.
	 */
	if (reset_flags & RESET_FLAG_SYSJUMP)
		return EC_SUCCESS;

	/* Check if any blocks are currently physically write-protected */
	for (i = 0; i < (LM4_FLASH_FSIZE + 1) / 32; i++) {
		if (LM4_FLASH_FMPPE[i] != 0xffffffff) {
			any_wp = 1;
			break;
		}
	}

	/* If nothing is write-protected, done. */
	if (!any_wp)
		return EC_SUCCESS;

	/*
	 * If the last reboot was a power-on reset, it should have cleared
	 * write-protect.  If it didn't, then the flash write protect registers
	 * have been permanently committed and we can't fix that.
	 */
	if (reset_flags & RESET_FLAG_POWER_ON)
		return EC_ERROR_ACCESS_DENIED;

	/* Otherwise, do a hard boot to clear the flash protection registers */
	system_reset(SYSTEM_RESET_HARD | SYSTEM_RESET_PRESERVE_FLAGS);

	/* That doesn't return, so if we're still here that's an error */
	return EC_ERROR_UNKNOWN;
}
