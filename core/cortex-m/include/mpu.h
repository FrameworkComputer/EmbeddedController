/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MPU module for Cortex-M3 */

#ifndef __CROS_EC_MPU_H
#define __CROS_EC_MPU_H

#include "common.h"

#define MPU_TYPE		REG32(0xe000ed90)
#define MPU_CTRL		REG32(0xe000ed94)
#define MPU_NUMBER		REG32(0xe000ed98)
#define MPU_BASE		REG32(0xe000ed9c)
#define MPU_SIZE		REG16(0xe000eda0)
#define MPU_ATTR		REG16(0xe000eda2)

#define MPU_CTRL_PRIVDEFEN	(1 << 2)
#define MPU_CTRL_HFNMIENA	(1 << 1)
#define MPU_CTRL_ENABLE		(1 << 0)
#define MPU_ATTR_NX		(1 << 12)
#define MPU_ATTR_NOACCESS	(0 << 8)
#define MPU_ATTR_FULLACCESS	(3 << 8)
/* Suggested in table 3-6 of Stellaris LM4F232H5QC Datasheet and table 38 of
 * STM32F10xxx Cortex-M3 programming manual for internal sram. */
#define MPU_ATTR_INTERNALSRAM	6

/**
 * Configure a region
 *
 * region: Number of the region to update
 * addr: Base address of the region
 * size: Size of the region in bytes
 * attr: Attribute of the region. Current value will be overwritten if enable
 * is set.
 * enable: Enables the region if non zero. Otherwise, disables the region.
 *
 * Returns EC_SUCCESS on success or EC_ERROR_INVAL if a parameter is invalid.
 */
int mpu_config_region(uint8_t region, uint32_t addr, uint32_t size,
		      uint16_t attr, uint8_t enable);

/**
 * Set a region non-executable.
 *
 * region: number of the region
 * addr: base address of the region
 * size: size of the region in bytes
 */
int mpu_nx_region(uint8_t region, uint32_t addr, uint32_t size);

/**
 * Enable MPU */
void mpu_enable(void);

/**
 * Returns the value of MPU type register
 *
 * Bit fields:
 * [15:8] Number of the data regions implemented or 0 if MPU is not present.
 * [1]    0: unified (no distinction between instruction and data)
 *        1: separated
 */
uint32_t mpu_get_type(void);

/* Location of iram.text */
extern char __iram_text_start;
extern char __iram_text_end;

/**
 * Lock down RAM
 */
int mpu_protect_ram(void);

/**
 * Initialize MPU.
 * It disables all regions if MPU is implemented. Otherwise, returns
 * EC_ERROR_UNIMPLEMENTED.
 */
int mpu_pre_init(void);

#endif /* __CROS_EC_MPU_H */
