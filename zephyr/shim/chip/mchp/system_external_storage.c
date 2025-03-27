/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bbram.h"
#include "clock_chip.h"
#include "common.h"
#include "config_chip.h"
#include "system.h"
#include "system_chip.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/bbram.h>

#include <soc.h>

#define MCHP_ECRO_WORD 0x4F524345u /* ASCII ECRO */
#define MCHP_ECRW_WORD 0x57524345u /* ASCII ECRW */
#define MCHP_PCR_NODE DT_INST(0, microchip_xec_pcr)

/* Build image type string in RO/RW image */
#ifdef CONFIG_CROS_EC_RO
const uint32_t mchp_image_type = MCHP_ECRO_WORD;
#elif CONFIG_CROS_EC_RW
const uint32_t mchp_image_type = MCHP_ECRW_WORD;
#else
#error "Unsupported image type!"
#endif

/* Holds next image to jump */
static enum ec_image next_image_copy = EC_IMAGE_UNKNOWN;
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
		flash_offset =
			CONFIG_PLATFORM_EC_FW_START_OFFSET_IN_EXT_SPI_FLASH +
			CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;
		flash_used = CONFIG_CROS_EC_RW_SIZE;
		break;
	case EC_IMAGE_RO:
	default: /* Jump to RO by default */
		flash_offset =
			CONFIG_PLATFORM_EC_FW_START_OFFSET_IN_EXT_SPI_FLASH +
			CONFIG_PLATFORM_EC_RO_HEADER_OFFSET;
		flash_used = CONFIG_CROS_EC_RO_SIZE;
		break;
	}

	/*
	 * Speed up FW download time by increasing clock freq of EC. It will
	 * restore to default in clock_init() later.
	 */
	clock_turbo();

	/* Before jumping, invalidate `next_image_copy` to let `mchp_image_type`
	 * figure out current image type.  */
	system_set_image_copy(EC_IMAGE_UNKNOWN);

	/* MCHP Read selected image from SPI flash into SRAM
	 * Need a jump to little-fw (LFW).
	 */
	system_download_from_flash(flash_offset,
				   CONFIG_CROS_EC_PROGRAM_MEMORY_BASE,
				   flash_used,
				   (CONFIG_CROS_EC_PROGRAM_MEMORY_BASE + 4u));
}

uint32_t system_get_lfw_address(void)
{
	uint32_t jump_addr = (uint32_t)system_jump_to_booter;
	return jump_addr;
}

enum ec_image system_get_shrspi_image_copy(void)
{
	enum ec_image img = next_image_copy;

	if (next_image_copy == EC_IMAGE_UNKNOWN) {
		img = EC_IMAGE_RO;
		if (mchp_image_type == MCHP_ECRW_WORD) {
			img = EC_IMAGE_RW;
		}
	}

	return img;
}

/* Flash is not memory mapped. Store a flag indicating the image.
 * ECS WDT_CNT is register available to applications. It implements bits[3:0]
 * which are not reset by a watch dog event only by VTR/chip reset.
 * VBAT memory is safer only if the board has a stable VBAT power rail.
 */
void system_set_image_copy(enum ec_image copy)
{
	switch (copy) {
	case EC_IMAGE_RW:
	case EC_IMAGE_RW_B:
		next_image_copy = EC_IMAGE_RW;
		break;
	case EC_IMAGE_RO:
		next_image_copy = EC_IMAGE_RO;
		break;
	default:
		next_image_copy = EC_IMAGE_UNKNOWN;
	}
}
