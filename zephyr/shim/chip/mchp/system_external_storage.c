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

static const struct device *const bbram_dev =
	COND_CODE_1(DT_HAS_CHOSEN(cros_ec_bbram),
		    DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram)), NULL);

/* Build image type string in RO/RW image */
#ifdef CONFIG_CROS_EC_RO
const uint32_t mchp_image_type = MCHP_ECRO_WORD;
#elif CONFIG_CROS_EC_RW
const uint32_t mchp_image_type = MCHP_ECRW_WORD;
#else
#error "Unsupported image type!"
#endif

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
	enum ec_image img = EC_IMAGE_UNKNOWN;
	uint32_t value = 0u;

	if (bbram_dev) {
		if (!bbram_read(bbram_dev, BBRAM_REGION_OFFSET(ec_img_load),
				BBRAM_REGION_SIZE(ec_img_load),
				(uint8_t *)&value)) {
			img = (enum ec_image)(value & 0x7fu);
		}
	}

	if (img == EC_IMAGE_UNKNOWN) {
		img = EC_IMAGE_RO;
		if (mchp_image_type == MCHP_ECRW_WORD) {
			img = EC_IMAGE_RW;
		}
		system_set_image_copy(img);
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
	uint32_t value = (uint32_t)copy;

	if (!bbram_dev) {
		return;
	}

	switch (copy) {
	case EC_IMAGE_RW:
	case EC_IMAGE_RW_B:
		value = EC_IMAGE_RW;
		break;
	case EC_IMAGE_RO:
	default:
		value = EC_IMAGE_RO;
		break;
	}

	bbram_write(bbram_dev, BBRAM_REGION_OFFSET(ec_img_load),
		    BBRAM_REGION_SIZE(ec_img_load), (uint8_t *)&value);
}
