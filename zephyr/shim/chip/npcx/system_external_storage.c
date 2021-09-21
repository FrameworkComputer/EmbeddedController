/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock_chip.h"
#include "common.h"
#include "rom_chip.h"
#include "system.h"
#include "system_chip.h"

/* TODO (b:179900857) Make this implementation not npcx specific. */

#define NPCX_MDC_BASE_ADDR                0x4000C000
#define NPCX_FWCTRL                       REG8(NPCX_MDC_BASE_ADDR + 0x007)
#define NPCX_FWCTRL_RO_REGION             0
#define NPCX_FWCTRL_FW_SLOT               1

void system_jump_to_booter(void)
{
	enum API_RETURN_STATUS_T status __attribute__((unused));
	static uint32_t flash_offset;
	static uint32_t flash_used;
	static uint32_t addr_entry;

	/*
	 * Get memory offset and size for RO/RW regions.
	 * Both of them need 16-bytes alignment since GDMA burst mode.
	 */
	switch (system_get_shrspi_image_copy()) {
	case EC_IMAGE_RW:
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_STORAGE_OFF;
		flash_used = CONFIG_RW_SIZE;
		break;
#ifdef CONFIG_RW_B
	case EC_IMAGE_RW_B:
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_B_STORAGE_OFF;
		flash_used = CONFIG_RW_SIZE;
		break;
#endif
	case EC_IMAGE_RO:
	default: /* Jump to RO by default */
		flash_offset = CONFIG_EC_PROTECTED_STORAGE_OFF +
				CONFIG_RO_STORAGE_OFF;
		flash_used = CONFIG_RO_SIZE;
		break;
	}

	/* Make sure the reset vector is inside the destination image */
	addr_entry = *(uintptr_t *)(flash_offset +
				    CONFIG_MAPPED_STORAGE_BASE + 4);

	/*
	 * Speed up FW download time by increasing clock freq of EC. It will
	 * restore to default in clock_init() later.
	 */
	clock_turbo();

/*
 * npcx9 Rev.1 has the problem for download_from_flash API.
 * Workwaroud it by executing the system_download_from_flash function
 * in the suspend RAM like npcx5.
 * TODO: Removing npcx9 when Rev.2 is available.
 */
	/* Bypass for GMDA issue of ROM api utilities */
#if defined(CONFIG_SOC_SERIES_NPCX5) || \
	defined(CONFIG_PLATFORM_EC_WORKAROUND_FLASH_DOWNLOAD_API)
	system_download_from_flash(
		flash_offset,      /* The offset of the data in spi flash */
		CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of downloaded data */
		flash_used,        /* Number of bytes to download      */
		addr_entry         /* jump to this address after download */
	);
#else
	download_from_flash(
		flash_offset,      /* The offset of the data in spi flash */
		CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of downloaded data */
		flash_used,        /* Number of bytes to download      */
		SIGN_NO_CHECK,     /* Need CRC check or not               */
		addr_entry,        /* jump to this address after download */
		&status            /* Status fo download */
	);
#endif
}

uint32_t system_get_lfw_address()
{
	/*
	 * In A3 version, we don't use little FW anymore
	 * We provide the alternative function in ROM
	 */
	uint32_t jump_addr = (uint32_t)system_jump_to_booter;
	return jump_addr;
}

enum ec_image system_get_shrspi_image_copy(void)
{
	/* TODO (b:179900857) Make this implementation not npcx specific. */
	if (IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION)) {
		/* RO image */
#ifdef CHIP_HAS_RO_B
		if (!IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT))
			return EC_IMAGE_RO_B;
#endif
		return EC_IMAGE_RO;
	} else {
#ifdef CONFIG_RW_B
		/* RW image */
		if (!IS_BIT_SET(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT))
			/* Slot A */
			return EC_IMAGE_RW_B;
#endif
		return EC_IMAGE_RW;
	}
}

void system_set_image_copy(enum ec_image copy)
{
	/* TODO (b:179900857) Make this implementation not npcx specific. */
	switch (copy) {
	case EC_IMAGE_RW:
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
#ifdef CONFIG_RW_B
	case EC_IMAGE_RW_B:
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		CLEAR_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
#endif
	default:
		/* Fall through to EC_IMAGE_RO */
	case EC_IMAGE_RO:
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_RO_REGION);
		SET_BIT(NPCX_FWCTRL, NPCX_FWCTRL_FW_SLOT);
		break;
	}
}
