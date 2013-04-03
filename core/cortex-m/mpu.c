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
 * region: number of the region to update
 * addr: base address of the region
 * size_bit: size of the region in power of two.
 * attr: attribute of the region. Current value will be overwritten if enable
 * is set.
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

int mpu_config_region(uint8_t region, uint32_t addr, uint32_t size,
		      uint16_t attr, uint8_t enable)
{
	int size_bit = 0;

	if (!size)
		return -EC_ERROR_INVAL;
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

int mpu_nx_region(uint8_t region, uint32_t addr, uint32_t size)
{
	return mpu_config_region(
		region, addr, size,
		MPU_ATTR_NX | MPU_ATTR_FULLACCESS | MPU_ATTR_INTERNALSRAM, 1);
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

/**
 * Prevent code from running on RAM.
 * We need to allow execution from iram.text. Using higher region# allows us to
 * do 'whitelisting' (lock down all then create exceptions).
 */
int mpu_protect_ram(void)
{
	int ret;

	ret = mpu_nx_region(0, CONFIG_RAM_BASE, CONFIG_RAM_SIZE);
	if (ret != EC_SUCCESS)
		return ret;

	ret = mpu_config_region(
		7, (uint32_t)&__iram_text_start,
		(uint32_t)(&__iram_text_end - &__iram_text_start),
		MPU_ATTR_FULLACCESS | MPU_ATTR_INTERNALSRAM, 1);

	return ret;
}

int mpu_pre_init(void)
{
	int i;

	if (mpu_get_type() != 0x00000800)
		return EC_ERROR_UNIMPLEMENTED;

	mpu_disable();
	for (i = 0; i < 8; ++i) {
		mpu_config_region(
			i, CONFIG_RAM_BASE, CONFIG_RAM_SIZE,
			MPU_ATTR_FULLACCESS | MPU_ATTR_INTERNALSRAM, 0);
	}

	return EC_SUCCESS;
}
