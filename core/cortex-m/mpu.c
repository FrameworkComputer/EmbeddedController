/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MPU module for Chrome EC */

#include "mpu.h"
#include "console.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/* Region assignment. 7 as the highest, a higher index has a higher priority.
 * For example, using 7 for .iram.text allows us to mark entire RAM XN except
 * .iram.text, which is used for hibernation. */
enum mpu_region {
	REGION_IRAM = 0,          /* For internal RAM */
	REGION_FLASH_MEMORY = 1,  /* For flash memory */
	REGION_IRAM_TEXT = 7      /* For *.(iram.text) */
};

/**
 * Update a memory region.
 *
 * region: index of the region to update
 * addr: base address of the region
 * size_bit: size of the region in power of two.
 * attr: attribute bits. Current value will be overwritten if enable is true.
 * enable: enables the region if non zero. Otherwise, disables the region.
 *
 * Based on 3.1.4.1 'Updating an MPU Region' of Stellaris LM4F232H5QC Datasheet
 */
static void mpu_update_region(uint8_t region, uint32_t addr, uint8_t size_bit,
			      uint16_t attr, uint8_t enable)
{
	asm volatile("isb; dsb;");

	MPU_NUMBER = region;
	MPU_SIZE &= ~1;					/* Disable */
	if (enable) {
		MPU_BASE = addr;
		MPU_ATTR = attr;
		MPU_SIZE = (size_bit - 1) << 1 | 1;	/* Enable */
	}

	asm volatile("isb; dsb;");
}

/**
 * Configure a region
 *
 * region: index of the region to update
 * addr: Base address of the region
 * size: Size of the region in bytes
 * attr: Attribute bits. Current value will be overwritten if enable is set.
 * enable: Enables the region if non zero. Otherwise, disables the region.
 *
 * Returns EC_SUCCESS on success or EC_ERROR_INVAL if a parameter is invalid.
 */
static int mpu_config_region(uint8_t region, uint32_t addr, uint32_t size,
			     uint16_t attr, uint8_t enable)
{
	int size_bit = 0;

	if (!size)
		return EC_SUCCESS;
	while (!(size & 1)) {
		size_bit++;
		size >>= 1;
	}
	/* Region size must be a power of 2 (size == 0) and equal or larger than
	 * 32 (size_bit >= 5) */
	if (size > 1 || size_bit < 5)
		return -EC_ERROR_INVAL;

	mpu_update_region(region, addr, size_bit, attr, enable);

	return EC_SUCCESS;
}

/**
 * Set a region non-executable and read-write.
 *
 * region: index of the region
 * addr: base address of the region
 * size: size of the region in bytes
 * texscb: TEX and SCB bit field
 */
static int mpu_lock_region(uint8_t region, uint32_t addr, uint32_t size,
			   uint8_t texscb)
{
	return mpu_config_region(region, addr, size,
				 MPU_ATTR_XN | MPU_ATTR_RW_RW | texscb, 1);
}

/**
 * Set a region executable and read-write.
 *
 * region: index of the region
 * addr: base address of the region
 * size: size of the region in bytes
 * texscb: TEX and SCB bit field
 */
static int mpu_unlock_region(uint8_t region, uint32_t addr, uint32_t size,
			     uint8_t texscb)
{
	return mpu_config_region(region, addr, size,
				 MPU_ATTR_RW_RW | texscb, 1);
}

void mpu_enable(void)
{
	MPU_CTRL |= MPU_CTRL_PRIVDEFEN | MPU_CTRL_HFNMIENA | MPU_CTRL_ENABLE;
}

void mpu_disable(void)
{
	MPU_CTRL &= ~(MPU_CTRL_PRIVDEFEN | MPU_CTRL_HFNMIENA | MPU_CTRL_ENABLE);
}

uint32_t mpu_get_type(void)
{
	return MPU_TYPE;
}

int mpu_protect_ram(void)
{
	int ret;
	ret = mpu_lock_region(REGION_IRAM, CONFIG_RAM_BASE,
			      CONFIG_RAM_SIZE, MPU_ATTR_INTERNAL_SRAM);
	if (ret != EC_SUCCESS)
		return ret;
	ret = mpu_unlock_region(
		REGION_IRAM_TEXT, (uint32_t)&__iram_text_start,
		(uint32_t)(&__iram_text_end - &__iram_text_start),
		MPU_ATTR_INTERNAL_SRAM);
	return ret;
}

int mpu_lock_ro_flash(void)
{
	return mpu_lock_region(REGION_FLASH_MEMORY, CONFIG_FW_RO_OFF,
			       CONFIG_FW_IMAGE_SIZE, MPU_ATTR_FLASH_MEMORY);
}

int mpu_lock_rw_flash(void)
{
	return mpu_lock_region(REGION_FLASH_MEMORY, CONFIG_FW_RW_OFF,
			       CONFIG_FW_RW_SIZE, MPU_ATTR_FLASH_MEMORY);
}

int mpu_pre_init(void)
{
	int i;

	if (mpu_get_type() != 0x00000800)
		return EC_ERROR_UNIMPLEMENTED;

	mpu_disable();
	for (i = 0; i < 8; ++i)
		mpu_config_region(i, CONFIG_RAM_BASE, CONFIG_RAM_SIZE, 0, 0);

	return EC_SUCCESS;
}
