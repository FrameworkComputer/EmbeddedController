/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "host_command.h"
#include "shared_mem.h"
#include "spi.h"
#include "spi_flash.h"
#include "system.h"
#include "util.h"
#include "hooks.h"

#define PAGE_SIZE 256

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */
#define FLASH_HOOK_VERSION 1

static int entire_flash_locked;

/* The previous write protect state before sys jump */

struct flash_wp_state {
	int entire_flash_locked;
};

/**
 * Read from physical flash.
 *
 * @param offset        Flash offset to write.
 * @param size          Number of bytes to write.
 * @param data          Destination buffer for data.
 */
int crec_flash_physical_read(int offset, int size, char *data)
{
	return spi_flash_read(data, offset, size);
}

/**
 * Write to physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE.
 *
 * @param offset        Flash offset to write.
 * @param size          Number of bytes to write.
 * @param data          Data to write to flash.  Must be 32-bit aligned.
 */
int crec_flash_physical_write(int offset, int size, const char *data)
{
	int ret = EC_SUCCESS;
	int i, write_size;

	if (entire_flash_locked)
		return EC_ERROR_ACCESS_DENIED;

	/* Fail if offset, size, and data aren't at least word-aligned */
	if ((offset | size | (uint32_t)(uintptr_t)data) & 3)
		return EC_ERROR_INVAL;

	for (i = 0; i < size; i += write_size) {
		write_size = MIN((size - i), SPI_FLASH_MAX_WRITE_SIZE);
		ret = spi_flash_write(offset + i, write_size,
				      (uint8_t *)data + i);
		if (ret != EC_SUCCESS)
			break;
	}
	return ret;
}

/**
 * Erase physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE.
 *
 * @param offset        Flash offset to erase.
 * @param size          Number of bytes to erase.
 */
int crec_flash_physical_erase(int offset, int size)
{
	int ret;

	if (entire_flash_locked)
		return EC_ERROR_ACCESS_DENIED;

	ret = spi_flash_erase(offset, size);
	return ret;
}

/**
 * Read physical write protect setting for a flash bank.
 *
 * @param bank    Bank index to check.
 * @return        non-zero if bank is protected until reboot.
 */
int crec_flash_physical_get_protect(int bank)
{
	return spi_flash_check_protect(bank * CONFIG_FLASH_BANK_SIZE,
				       CONFIG_FLASH_BANK_SIZE);
}

/**
 * Protect flash now.
 *
 * This is always successful, and only emulates "now" protection
 *
 * @param all      Protect all (=1) or just read-only
 * @return         non-zero if error.
 */
int crec_flash_physical_protect_now(int all)
{
	if (all)
		entire_flash_locked = 1;

	/*
	 * RO "now" protection is not currently implemented. If needed, it
	 * can be added by splitting the entire_flash_locked variable into
	 * and RO and RW vars, and setting + checking the appropriate var
	 * as required.
	 */
	return EC_SUCCESS;
}

/**
 * Return flash protect state flags from the physical layer.
 *
 * This should only be called by flash_get_protect().
 *
 * Uses the EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	if (spi_flash_check_protect(CONFIG_WP_STORAGE_OFF,
				    CONFIG_WP_STORAGE_SIZE)) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;
	}

	if (entire_flash_locked)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	return flags;
}

/**
 * Return the valid flash protect flags.
 *
 * @return   A combination of EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

/**
 * Return the writable flash protect flags.
 *
 * @param    cur_flags The current flash protect flags.
 * @return   A combination of EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;
	enum spi_flash_wp wp_status = SPI_WP_NONE;

	wp_status = spi_flash_check_wp();

	if (wp_status == SPI_WP_NONE ||
	    (wp_status == SPI_WP_HARDWARE &&
	     !(cur_flags & EC_FLASH_PROTECT_GPIO_ASSERTED)))
		ret = EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;

	if (!entire_flash_locked)
		ret |= EC_FLASH_PROTECT_ALL_NOW;

	return ret;
}

/**
 * Enable write protect for the specified range.
 *
 * Once write protect is enabled, it will stay enabled until HW PIN is
 * de-asserted and SRP register is unset.
 *
 * However, this implementation treats EC_FLASH_PROTECT_ALL_AT_BOOT as
 * EC_FLASH_PROTECT_RO_AT_BOOT but tries to remember if "all" region
 * is protected.
 *
 * @param new_flags to protect (only EC_FLASH_PROTECT_*_AT_BOOT are
 * taken care of)
 * @return              EC_SUCCESS, or nonzero if error.
 */
int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	int offset, size, ret;
	enum spi_flash_wp flashwp = SPI_WP_NONE;

	if ((new_flags & (EC_FLASH_PROTECT_RO_AT_BOOT |
			  EC_FLASH_PROTECT_ALL_AT_BOOT)) == 0) {
		/* Clear protection */
		offset = size = 0;
		flashwp = SPI_WP_NONE;
	} else {
		if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT)
			entire_flash_locked = 1;

		offset = CONFIG_WP_STORAGE_OFF;
		size = CONFIG_WP_STORAGE_SIZE;
		flashwp = SPI_WP_HARDWARE;
	}

	ret = spi_flash_set_protect(offset, size);
	if (ret == EC_SUCCESS)
		ret = spi_flash_set_wp(flashwp);
	return ret;
}

/**
 * Initialize the module.
 *
 * Applies at-boot protection settings if necessary.
 */
int crec_flash_pre_init(void)
{
	crec_flash_physical_restore_state();
	return EC_SUCCESS;
}

int crec_flash_physical_restore_state(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	int version, size;
	const struct flash_wp_state *prev;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection. Nothing additional needs to be done.
	 */
	if (reset_flags & EC_RESET_FLAG_SYSJUMP) {
		prev = (const struct flash_wp_state *)system_get_jump_tag(
			FLASH_SYSJUMP_TAG, &version, &size);
		if (prev && version == FLASH_HOOK_VERSION &&
		    size == sizeof(*prev))
			entire_flash_locked = prev->entire_flash_locked;
		return 1;
	}

	return 0;
}

/*****************************************************************************/
/* Hooks */

static void flash_preserve_state(void)
{
	struct flash_wp_state state;

	state.entire_flash_locked = entire_flash_locked;

	system_add_jump_tag(FLASH_SYSJUMP_TAG, FLASH_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, flash_preserve_state, HOOK_PRIO_DEFAULT);
