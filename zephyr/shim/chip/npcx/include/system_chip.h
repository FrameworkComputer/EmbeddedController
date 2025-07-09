/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SYSTEM_CHIP_H_
#define __CROS_EC_SYSTEM_CHIP_H_

#define SET_BIT(reg, bit) ((reg) |= (0x1 << (bit)))
#define CLEAR_BIT(reg, bit) ((reg) &= (~(0x1 << (bit))))

/*****************************************************************************/
/* Memory mapping */
#define CONFIG_LPRAM_BASE 0x40001400 /* memory address of lpwr ram */
#define CONFIG_LPRAM_SIZE 0x00000620 /* 1568B low power ram */

/******************************************************************************/
/* Optional M4 Registers */
#define CPU_MPU_CTRL REG32(0xE000ED94)
#define CPU_MPU_RNR REG32(0xE000ED98)
#define CPU_MPU_RBAR REG32(0xE000ED9C)
#define CPU_MPU_RASR REG32(0xE000EDA0)

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

/*
 * Configure the specific memory addresses in the the MPU
 * (Memory Protection Unit) for Nuvoton different chip series.
 */
void system_mpu_config(void);

/* The utilities and variables depend on npcx chip family */
#if defined(CONFIG_SOC_SERIES_NPCX5) || \
	defined(CONFIG_PLATFORM_EC_WORKAROUND_FLASH_DOWNLOAD_API)
/* Bypass for GMDA issue of ROM api utilities only on npcx5 series or if
 * CONFIG_PLATFORM_EC_WORKAROUND_FLASH_DOWNLOAD_API is defined.
 */
void system_download_from_flash(uint32_t srcAddr, uint32_t dstAddr,
				uint32_t size, uint32_t exeAddr);

/* Begin address for hibernate utility; defined in linker script */
extern unsigned int __flash_lpfw_start;

/* End address for hibernate utility; defined in linker script */
extern unsigned int __flash_lpfw_end;

/* Begin address for little FW; defined in linker script */
extern unsigned int __flash_lplfw_start;

/* End address for little FW; defined in linker script */
extern unsigned int __flash_lplfw_end;
#endif

#endif // __CROS_EC_SYSTEM_CHIP_H_
