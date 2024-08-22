/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MPU module for Cortex-M3 */

#ifndef __CROS_EC_MPU_H
#define __CROS_EC_MPU_H

#include "common.h"
#include "config.h" /* chips might override MPU attribute settings */

/*
 * ARMv7-M SRAM region
 */
#define CORTEX_M_SRAM_BASE 0x20000000

/*
 * Region assignment. 7 as the highest, a higher index has a higher priority.
 * For example, using 7 for .iram.text allows us to mark entire RAM XN except
 * .iram.text, which is used for hibernation.
 * Region assignment is currently wasteful and can be changed if more
 * regions are needed in the future. For example, a second region may not
 * be necessary for all types, and REGION_CODE_RAM / REGION_STORAGE can be
 * made mutually exclusive.
 */
enum mpu_region {
	REGION_DATA_RAM = 0, /* For internal data RAM */
	REGION_DATA_RAM2 = 1, /* Second region for unaligned size */
	REGION_CODE_RAM = 2, /* For internal code RAM */
	REGION_CODE_RAM2 = 3, /* Second region for unaligned size */
	REGION_STORAGE = 4, /* For mapped internal storage */
	REGION_STORAGE2 = 5, /* Second region for unaligned size */
	REGION_DATA_RAM_TEXT = 6, /* Exempt region of data RAM */
	REGION_CHIP_RESERVED = 7, /* Reserved for use in chip/ */
	/* only for chips with MPU supporting 16 regions */
	REGION_UNCACHED_RAM = 8, /* For uncached data RAM */
	REGION_UNCACHED_RAM2 = 9, /* Second region for unaligned size */
	REGION_ROLLBACK = 10, /* For rollback */
};

#define MPU_TYPE REG32(0xe000ed90)
#define MPU_CTRL REG32(0xe000ed94)
#define MPU_NUMBER REG32(0xe000ed98)
#define MPU_BASE REG32(0xe000ed9c)
#define MPU_SIZE REG16(0xe000eda0)
#define MPU_ATTR REG16(0xe000eda2)

/*
 * See ARM v7-M Architecture Reference Manual
 * Section B3.5.5 MPU Type Register, MPU_TYPE
 */
#define MPU_TYPE_UNIFIED_MASK 0x00FF0001
#define MPU_TYPE_REG_COUNT(t) (((t) >> 8) & 0xFF)

#define MPU_CTRL_PRIVDEFEN BIT(2)
#define MPU_CTRL_HFNMIENA BIT(1)
#define MPU_CTRL_ENABLE BIT(0)

/*
 * Minimum region size is 32 bytes, 5 bits of address space
 */
#define MPU_SIZE_BITS_MIN 5

/*
 * XN (execute never) bit. It's bit 12 if accessed by halfword.
 *   0: XN off
 *   1: XN on
 */
#define MPU_ATTR_XN BIT(12)

/* AP bit. See table 3-5 of Stellaris LM4F232H5QC datasheet for details */
#define MPU_ATTR_NO_NO (0 << 8) /* previleged no access, unprev no access */
#define MPU_ATTR_RW_NO (1 << 8) /* previleged ReadWrite, unprev no access */
#define MPU_ATTR_RW_RO (2 << 8) /* previleged ReadWrite, unprev Read-only */
#define MPU_ATTR_RW_RW (3 << 8) /* previleged ReadWrite, unprev ReadWrite */
#define MPU_ATTR_RO_NO (5 << 8) /* previleged Read-only, unprev no access */

/* Suggested value for TEX S/C/B bit. See table 3-6 of Stellaris LM4F232H5QC
 * datasheet and table 38 of STM32F10xxx Cortex-M3 programming manual. */
#ifndef MPU_ATTR_INTERNAL_SRAM
#define MPU_ATTR_INTERNAL_SRAM 6 /* for Internal SRAM */
#endif
#ifndef MPU_ATTR_FLASH_MEMORY
#define MPU_ATTR_FLASH_MEMORY 2 /* for flash memory */
#endif

/* Represent RW with at most 2 MPU regions. */
#define MAX_RW_REGIONS 2
struct mpu_rw_regions {
	int num_regions;
	uint32_t addr[MAX_RW_REGIONS];
	uint32_t size[MAX_RW_REGIONS];
};

/**
 * Enable MPU
 */
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
 * Protect RAM from code execution
 */
int mpu_protect_data_ram(void);

/**
 * Protect code RAM from being overwritten
 */
int mpu_protect_code_ram(void);

/**
 * Protect internal mapped flash memory from code execution
 */
int mpu_lock_ro_flash(void);
int mpu_lock_rw_flash(void);

/**
 * Protect/unprotect rollback region readback.
 */
int mpu_lock_rollback(int lock);

/**
 * Initialize MPU.
 * It disables all regions if MPU is implemented. Otherwise, returns
 * EC_ERROR_UNIMPLEMENTED.
 */
int mpu_pre_init(void);

/**
 * Protects Code RAM if CONFIG_PROTECT_CODE_RAM is enabled.
 */
void mpu_post_init(void);

#endif /* __CROS_EC_MPU_H */
