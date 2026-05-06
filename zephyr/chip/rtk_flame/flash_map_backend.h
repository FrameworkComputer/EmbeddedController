/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * For Chipset: RTK EC
 *
 * Function: RTK Flash Utility
 */

#ifndef __FLASH_MAP_BACKEND_H__
#define __FLASH_MAP_BACKEND_H__

/*********************
 *      INCLUDES
 *********************/

#include "flash_spic.h"

#include <stdint.h>
/*********************
 *      DEFINES
 *********************/

#define FLASH_SIZE_32MB 0x02000000ul
#define FLASH_SIZE_16MB 0x01000000ul

#define FLASH_SECTOR_EARSE_SIZE 4096
#define FLASH_PAGE_PROGRAM_SIZE 256

/* General flash opcode. */
#define FLASH_CMD_WREN 0x06 // write enable
#define FLASH_CMD_WRDI 0x04 // write disable
#define FLASH_CMD_WRSR 0x01 // write status register
#define FLASH_CMD_WRSR2 0x31 // write status register 2
#define FLASH_CMD_RDID 0x9F // read identification
#define FLASH_CMD_RDSR 0x05 // read status register
#define FLASH_CMD_RDSR2 0x35 // read status register-2
#define FLASH_CMD_RDSR3 0x15 // read status register-3
#define FLASH_CMD_READ 0x03 // read data
#define FLASH_CMD_FREAD 0x0B // fast read data
#define FLASH_CMD_RDSFDP 0x5A // Read SFDP
#define FLASH_CMD_RES 0xAB // Read Electronic ID
#define FLASH_CMD_REMS 0x90 // Read Electronic Manufacturer & Device ID
#define FLASH_CMD_DREAD 0x3B // Double Output Mode command
#define FLASH_CMD_SE 0x20 // Sector Erase for 3-byte addressing
#define FLASH_CMD_SE_4B 0x21 // Sector Erase for 4-byte addressing
#define FLASH_CMD_BE 0xD8 // 0x52 //64K Block Erase
#define FLASH_CMD_CE 0xC7 // Chip Erase(or 0x60)
#define FLASH_CMD_PP 0x02 // Page Program for 3-byte addressing
#define FLASH_CMD_PP_4B 0x12 // Page Program for 4-byte addressing
#define FLASH_CMD_DP 0xB9 // Deep Power Down
#define FLASH_CMD_RDP 0xAB // Release from Deep Power-Down
#define FLASH_CMD_2READ 0xBB // 2 x I/O read  command
#define FLASH_CMD_4READ 0xEB // 4 x I/O read  command
#define FLASH_CMD_QREAD 0x6B // 1I / 4O read command
#define FLASH_CMD_4PP 0x38 // quad page program
#define FLASH_CMD_FF 0xFF // Release Read Enhanced
#define FLASH_CMD_REMS2 0x92 // read ID for 2x I/O mode, diff with MXIC
#define FLASH_CMD_REMS4 0x94 // read ID for 4x I/O mode, diff with MXIC
#define FLASH_CMD_RDSCUR 0x48 // read security register,  diff with MXIC
#define FLASH_CMD_WRSCUR 0x42 // write security register, diff with MXIC
#define FLASH_CMD_EN_RST 0x66 // reset enable
#define FLASH_CMD_RST_DEV 0x99 // reset device

/* Support address 4 byte opcode for large size flash */
#define FLASH_CMD_EN4B 0xB7 // Enter 4-byte mode
#define FLASH_CMD_EX4B 0xE9 // Exit 4-byte mode

/* Bank addr access commands */
#define FLASH_CMD_EXTNADDR_WREAR 0xC5 // Write extended address register
#define FLASH_CMD_EXTNADDR_RDEAR 0xC8 // Read extended address register

/**********************
 *      TYPEDEFS
 **********************/

enum flash_addressing_mode {
	FLASH_ADDRESSING_3BYTE = 0,
	FLASH_ADDRESSING_4BYTE
};

/**********************
 *  GLOBAL PROTOTYPES
 **********************/
int32_t extr_flash_deinit(enum flash_addressing_mode mode);

int32_t extr_flash_pin_deinit(void);
int32_t intr_flash_pin_init(void);

int32_t flash_erase_sector(uint32_t address, enum flash_addressing_mode mode);
int32_t flash_read(uint8_t rdcmd, uint32_t address, uint8_t *data,
		   uint32_t size, enum flash_addressing_mode mode);
int32_t flash_program_page(uint32_t address, const uint8_t *data, uint32_t size,
			   enum flash_addressing_mode mode);
void slowtmr_dealy_us(uint32_t us);
int32_t flash_write_status_reg(uint8_t sr1, uint8_t sr2);

#endif /* __FLASH_MAP_BACKEND_H__ */
