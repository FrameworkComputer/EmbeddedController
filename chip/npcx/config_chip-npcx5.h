/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_CONFIG_CHIP_NPCX5_H
#define __CROS_EC_CONFIG_CHIP_NPCX5_H

/*
 * NPCX5 Series Device-Specific Information
 * Ex. NPCX5(M)(N)(G)
 * @param M: 7: 132-pins package, 8: 128-pins package
 * @param N: 5: 128KB RAM Size, 6: 256KB RAM Size
 * @param G: Google EC.
 */

/* Chip ID for all variants */
#define NPCX585G_CHIP_ID		0x12
#define NPCX575G_CHIP_ID		0x13
#define NPCX586G_CHIP_ID		0x16
#define NPCX576G_CHIP_ID		0x17

/*****************************************************************************/
/* Hardware features */

/* Number of UART modules. */
#define UART_MODULE_COUNT 1

/*
 * Number of I2C controllers. Controller 0 has 2 ports, so the chip has one
 * additional port.
 */
#define CONFIG_I2C_MULTI_PORT_CONTROLLER
/* Number of I2C controllers */
#define I2C_CONTROLLER_COUNT	4
/* Number of I2C ports */
#define I2C_PORT_COUNT		5

/*****************************************************************************/
/* Memory mapping */
#define NPCX_BTRAM_SIZE		0x800 /* 2KB data ram used by booter. */
#define CONFIG_RAM_BASE		0x200C0000 /* memory address of data ram */
#define CONFIG_DATA_RAM_SIZE	0x00008000 /* Size of data RAM */
#define CONFIG_RAM_SIZE		(CONFIG_DATA_RAM_SIZE - NPCX_BTRAM_SIZE)
#define CONFIG_LPRAM_BASE	0x40001600 /* memory address of lpwr ram */
#define CONFIG_LPRAM_SIZE	0x00000620 /* 1568B low power ram */

/* Use chip variant to specify the size and start address of program memory */
#if defined(CHIP_VARIANT_NPCX5M5G)
/* 96KB RAM for FW code */
#define NPCX_PROGRAM_MEMORY_SIZE (96 * 1024)
/* program memory base address for 96KB Code RAM (ie. 0x100C0000 - 96KB) */
#define CONFIG_PROGRAM_MEMORY_BASE 0x100A8000
#elif defined(CHIP_VARIANT_NPCX5M6G)
/* 224KB RAM for FW code */
#define NPCX_PROGRAM_MEMORY_SIZE (224 * 1024)
/* program memory base address for 224KB Code RAM (ie. 0x100C0000 - 224KB) */
#define CONFIG_PROGRAM_MEMORY_BASE 0x10088000
#else
#error "Unsupported chip variant"
#endif

/* Total RAM size checking for npcx ec */
#define NPCX_RAM_SIZE (CONFIG_DATA_RAM_SIZE + NPCX_PROGRAM_MEMORY_SIZE)
#if defined(CHIP_VARIANT_NPCX5M5G)
/* 128KB RAM in NPCX5M5G */
#if (NPCX_RAM_SIZE != 0x20000)
#error "Wrong memory mapping layout for NPCX5M5G"
#endif
#elif defined(CHIP_VARIANT_NPCX5M6G)
/* 256KB RAM in NPCX5M6G */
#if (NPCX_RAM_SIZE != 0x40000)
#error "Wrong memory mapping layout for NPCX5M6G"
#endif
#endif

#endif /* __CROS_EC_CONFIG_CHIP_NPCX5_H */
