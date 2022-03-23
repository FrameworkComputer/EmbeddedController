/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include <drivers/syscon.h>

#include "clock_chip.h"
#include "common.h"
#include "system.h"
#include "system_chip.h"


/*
 * Make sure CONFIG_XXX flash offsets are correct for MEC172x 512KB SPI flash.
 */
void system_jump_to_booter(void)
{
	static uint32_t flash_offset;
	static uint32_t flash_used;

	__disable_irq();

	/*
	 * Get memory offset and size for RO/RW regions.
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
		flash_offset = 0x100; /* 256 bytes */
		flash_used = (352 * 1024);
		break;
	}

	/*
	 * Speed up FW download time by increasing clock freq of EC. It will
	 * restore to default in clock_init() later.
	 */
	clock_turbo();

	/* MCHP Read selected image from SPI flash into SRAM
	 * Need a jump to little-fw (LFW).
	 * MEC172x Boot-ROM load API is probably not usuable for this.
	 */
	system_download_from_flash(flash_offset, 0xC0000u, flash_used, 0xC0004);
}

uint32_t system_get_lfw_address(void)
{
	uint32_t jump_addr = (uint32_t)system_jump_to_booter;
	return jump_addr;
}

enum ec_image system_get_shrspi_image_copy(void)
{
	return EC_IMAGE_RO;
}

/*
 * This configures HW to point to EC_RW or EC_RO.
 */
void system_set_image_copy(enum ec_image copy)
{
	/* TODO(b/226599277): check if further development is requested */
}
