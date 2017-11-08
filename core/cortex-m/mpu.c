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

/**
 * Update a memory region.
 *
 * region: index of the region to update
 * addr: base address of the region
 * size_bit: size of the region in power of two.
 * attr: attribute bits. Current value will be overwritten if enable is true.
 * enable: enables the region if non zero. Otherwise, disables the region.
 * srd: subregion mask to partition region into 1/8ths, 0 = subregion enabled.
 *
 * Based on 3.1.4.1 'Updating an MPU Region' of Stellaris LM4F232H5QC Datasheet
 */
static void mpu_update_region(uint8_t region, uint32_t addr, uint8_t size_bit,
			      uint16_t attr, uint8_t enable, uint8_t srd)
{
	asm volatile("isb; dsb;");

	MPU_NUMBER = region;
	MPU_SIZE &= ~1;					/* Disable */
	if (enable) {
		MPU_BASE = addr;
		MPU_ATTR = attr;
		MPU_SIZE = (srd << 8) | ((size_bit - 1) << 1) | 1; /* Enable */
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
 * Returns EC_SUCCESS on success or -EC_ERROR_INVAL if a parameter is invalid.
 */
static int mpu_config_region(uint8_t region, uint32_t addr, uint32_t size,
			     uint16_t attr, uint8_t enable)
{
	int size_bit = 0;
	uint8_t blocks, srd1, srd2;

	if (!size)
		return EC_SUCCESS;

	/* Bit position of first '1' in size */
	size_bit = 31 - __builtin_clz(size);
	/* Min. region size is 32 bytes */
	if (size_bit < 5)
		return -EC_ERROR_INVAL;

	/* If size is a power of 2 then represent it with a single MPU region */
	if (POWER_OF_TWO(size)) {
		mpu_update_region(region, addr, size_bit, attr, enable, 0);
		return EC_SUCCESS;
	}

	/* Sub-regions are not supported for region <= 128 bytes */
	if (size_bit < 7)
		return -EC_ERROR_INVAL;
	/* Verify we can represent range with <= 2 regions */
	if (size & ~(0x3f << (size_bit - 5)))
		return -EC_ERROR_INVAL;

	/*
	 * Round up size of first region to power of 2.
	 * Calculate the number of fully occupied blocks (block size =
	 * region size / 8) in the first region.
	 */
	blocks = size >> (size_bit - 2);

	/* Represent occupied blocks of two regions with srd mask. */
	srd1 = (1 << blocks) - 1;
	srd2 = (1 << ((size >> (size_bit - 5)) & 0x7)) - 1;

	/*
	 * Second region not supported for DATA_RAM_TEXT, also verify size of
	 * second region is sufficient to support sub-regions.
	 */
	if (srd2 && (region == REGION_DATA_RAM_TEXT || size_bit < 10))
		return -EC_ERROR_INVAL;

	/* Write first region. */
	mpu_update_region(region, addr, size_bit + 1, attr, enable, ~srd1);

	/*
	 * Second protection region (if necessary) begins at the first block
	 * we marked unoccupied in the first region.
	 * Size of the second region is the block size of first region.
	 */
	addr += (1 << (size_bit - 2)) * blocks;

	/*
	 * Now represent occupied blocks in the second region. It's possible
	 * that the first region completely represented the occupied area, if
	 * so then no second protection region is required.
	 */
	if (srd2)
		mpu_update_region(region + 1, addr, size_bit - 2, attr, enable,
				  ~srd2);

	return EC_SUCCESS;
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

int mpu_protect_data_ram(void)
{
	int ret;

	/* Prevent code execution from data RAM */
	ret = mpu_config_region(REGION_DATA_RAM,
				CONFIG_RAM_BASE,
				CONFIG_DATA_RAM_SIZE,
				MPU_ATTR_XN |
				MPU_ATTR_RW_RW |
				MPU_ATTR_INTERNAL_SRAM,
				1);
	if (ret != EC_SUCCESS)
		return ret;

	/* Exempt the __iram_text section */
	return mpu_unlock_region(
		REGION_DATA_RAM_TEXT, (uint32_t)&__iram_text_start,
		(uint32_t)(&__iram_text_end - &__iram_text_start),
		MPU_ATTR_INTERNAL_SRAM);
}

#ifdef CONFIG_EXTERNAL_STORAGE
int mpu_protect_code_ram(void)
{
	/* Prevent write access to code RAM */
	return mpu_config_region(REGION_STORAGE,
				 CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF,
				 CONFIG_RO_SIZE,
				 MPU_ATTR_RO_NO | MPU_ATTR_INTERNAL_SRAM,
				 1);
}
#else
int mpu_lock_ro_flash(void)
{
	/* Prevent execution from internal mapped RO flash */
	return mpu_config_region(REGION_STORAGE,
				 CONFIG_MAPPED_STORAGE_BASE + CONFIG_RO_MEM_OFF,
				 CONFIG_RO_SIZE,
				 MPU_ATTR_XN | MPU_ATTR_RW_RW |
				 MPU_ATTR_FLASH_MEMORY, 1);
}

int mpu_lock_rw_flash(void)
{
	/* Prevent execution from internal mapped RW flash */
	return mpu_config_region(REGION_STORAGE,
				 CONFIG_MAPPED_STORAGE_BASE + CONFIG_RW_MEM_OFF,
				 CONFIG_RW_SIZE,
				 MPU_ATTR_XN | MPU_ATTR_RW_RW |
				 MPU_ATTR_FLASH_MEMORY, 1);
}
#endif /* !CONFIG_EXTERNAL_STORAGE */

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
