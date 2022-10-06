/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/syscon.h>

#include "clock_chip.h"
#include "common.h"
#include "rom_chip.h"
#include "system.h"
#include "system_chip.h"

/* TODO (b:179900857) Make this implementation not npcx specific. */

static const struct device *mdc_dev = DEVICE_DT_GET(DT_NODELABEL(mdc));

/*
 * b/218820985: The FWCTL register reset to 0xFF on multiple reads.
 * Read the register once, and then cache all writes to this register
 *
 * TODO: if this is a chip bug, this should be managed by the syscon driver
 * upstream.
 */
static uint32_t fwctrl_cached = 0xFFFFFFFF;

#ifdef CONFIG_SOC_SERIES_NPCX7
#define NPCX_FWCTRL 0x007
#define NPCX_FWCTRL_RO_REGION 0
#define NPCX_FWCTRL_FW_SLOT 1
#elif defined(CONFIG_SOC_SERIES_NPCX9)
#define NPCX_FWCTRL 0x009
#define NPCX_FWCTRL_RO_REGION 6
#define NPCX_FWCTRL_FW_SLOT 7
#else
#error "Unsupported NPCX SoC series."
#endif

static uint32_t read_fwctrl(void)
{
	/* On first entry, read the value and cache it. */
	if (fwctrl_cached == 0xFFFFFFFF) {
		syscon_read_reg(mdc_dev, NPCX_FWCTRL, &fwctrl_cached);
	}

	return fwctrl_cached;
}

static void write_fwctrl(uint32_t fwctrl)
{
	syscon_write_reg(mdc_dev, NPCX_FWCTRL, fwctrl);
	fwctrl_cached = fwctrl;
}

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
		flash_offset =
			CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;
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
		flash_offset =
			CONFIG_EC_PROTECTED_STORAGE_OFF + CONFIG_RO_STORAGE_OFF;
		flash_used = CONFIG_RO_SIZE;
		break;
	}

	/* Make sure the reset vector is inside the destination image */
	addr_entry =
		*(uintptr_t *)(flash_offset + CONFIG_MAPPED_STORAGE_BASE + 4);

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
	system_download_from_flash(flash_offset, /* The offset of the data in
						    spi flash */
				   CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of
								  downloaded
								  data */
				   flash_used, /* Number of bytes to download */
				   addr_entry /* jump to this address after
						 download */
	);
#else
	download_from_flash(flash_offset, /* The offset of the data in spi flash
					   */
			    CONFIG_PROGRAM_MEMORY_BASE, /* RAM Addr of
							   downloaded data */
			    flash_used, /* Number of bytes to download      */
			    SIGN_NO_CHECK, /* Need CRC check or not */
			    addr_entry, /* jump to this address after download
					 */
			    &status /* Status fo download */
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
	uint32_t fwctrl;

	fwctrl = read_fwctrl();

	if (IS_BIT_SET(fwctrl, NPCX_FWCTRL_RO_REGION)) {
		/* RO image */
#ifdef CHIP_HAS_RO_B
		if (!IS_BIT_SET(fwctrl, NPCX_FWCTRL_FW_SLOT))
			return EC_IMAGE_RO_B;
#endif
		return EC_IMAGE_RO;
	} else {
#ifdef CONFIG_RW_B
		/* RW image */
		if (!IS_BIT_SET(fwctrl, NPCX_FWCTRL_FW_SLOT))
			/* Slot A */
			return EC_IMAGE_RW_B;
#endif
		return EC_IMAGE_RW;
	}
}

void system_set_image_copy(enum ec_image copy)
{
	uint32_t fwctrl;

	fwctrl = read_fwctrl();
	switch (copy) {
	case EC_IMAGE_RW:
		CLEAR_BIT(fwctrl, NPCX_FWCTRL_RO_REGION);
		SET_BIT(fwctrl, NPCX_FWCTRL_FW_SLOT);
		break;
#ifdef CONFIG_RW_B
	case EC_IMAGE_RW_B:
		CLEAR_BIT(fwctrl, NPCX_FWCTRL_RO_REGION);
		CLEAR_BIT(fwctrl, NPCX_FWCTRL_FW_SLOT);
		break;
#endif
	default:
		/* Fall through to EC_IMAGE_RO */
	case EC_IMAGE_RO:
		SET_BIT(fwctrl, NPCX_FWCTRL_RO_REGION);
		SET_BIT(fwctrl, NPCX_FWCTRL_FW_SLOT);
		break;
	}

	write_fwctrl(fwctrl);
}
