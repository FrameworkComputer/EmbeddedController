/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * STM32 bootloader protocol over I2C (AN4221).
 *
 * Implements the subset needed for firmware update: init, erase, write,
 * read, go.  Designed to run on the EC, using the cros-ec i2c_xfer() API.
 */

#ifndef __CROS_EC_STM32_BL_H
#define __CROS_EC_STM32_BL_H

#include <stdint.h>

/* STM32 bootloader response bytes */
#define STM32_BL_ACK	0x79
#define STM32_BL_NACK	0x1F
#define STM32_BL_BUSY	0x76

/* Command codes */
#define STM32_BL_CMD_GET	0x00
#define STM32_BL_CMD_GVR	0x01
#define STM32_BL_CMD_GID	0x02
#define STM32_BL_CMD_RM		0x11
#define STM32_BL_CMD_GO		0x21
#define STM32_BL_CMD_WM		0x31
#define STM32_BL_CMD_WM_NS	0x32
#define STM32_BL_CMD_ER		0x43
#define STM32_BL_CMD_EE		0x44
#define STM32_BL_CMD_EE_NS	0x45
#define STM32_BL_CMD_ERR	0xFF

/* Default STM32 bootloader I2C address (7-bit, per AN2606 for STM32U0) */
#define STM32_BL_I2C_ADDR	0x6A

/* Maximum bytes per write/read operation */
#define STM32_BL_MAX_DATA	256

/* Default flash base address */
#define STM32_BL_FLASH_BASE	0x08000000

/* Device info populated by stm32_bl_init() */
struct stm32_bl_dev {
	uint8_t version;	/* Bootloader version from GVR */
	uint8_t bl_version;	/* Bootloader version from GET */
	uint16_t pid;		/* Product ID from GID */
	uint8_t cmd_wm;		/* Write Memory command (0x31 or 0x32) */
	uint8_t cmd_er;		/* Erase command (0x43, 0x44, or 0x45) */
	uint8_t cmd_rm;		/* Read Memory command (0x11) */
	uint8_t cmd_go;		/* Go command (0x21) */
};

/*
 * Initialize: run GVR + GET + GID sequence, populate dev struct.
 * Returns EC_SUCCESS or EC_ERROR_*.
 */
int stm32_bl_init(int i2c_port, uint16_t addr, struct stm32_bl_dev *dev);

/*
 * Mass-erase all flash.  Takes up to 35 seconds with clock stretching.
 * Returns EC_SUCCESS or EC_ERROR_*.
 */
int stm32_bl_mass_erase(int i2c_port, uint16_t addr,
			const struct stm32_bl_dev *dev);

/*
 * Write up to 256 bytes to flash.  Address must be 4-byte aligned.
 * Returns EC_SUCCESS or EC_ERROR_*.
 */
int stm32_bl_write_memory(int i2c_port, uint16_t addr,
			  const struct stm32_bl_dev *dev,
			  uint32_t flash_addr, const uint8_t *data, int len);

/*
 * Read up to 256 bytes from flash.
 * Returns EC_SUCCESS or EC_ERROR_*.
 */
int stm32_bl_read_memory(int i2c_port, uint16_t addr,
			 const struct stm32_bl_dev *dev,
			 uint32_t flash_addr, uint8_t *data, int len);

/*
 * Jump to application at given address.
 * Returns EC_SUCCESS or EC_ERROR_*.
 */
int stm32_bl_go(int i2c_port, uint16_t addr,
		const struct stm32_bl_dev *dev, uint32_t go_addr);

#endif /* __CROS_EC_STM32_BL_H */
