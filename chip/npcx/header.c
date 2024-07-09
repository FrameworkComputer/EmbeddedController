/*
 * Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Booter header for Chrome EC.
 *
 * This header is used by Nuvoton EC Booter.
 */

#include "config.h"
#include "registers.h"

#include <stdint.h>

/* Signature used by fw header */
#define SIG_FW_EC 0x2A3B4D5E

/* Definition used by error detection configuration */
#define CHECK_CRC 0x00
#define CHECK_CHECKSUM 0x01
#define ERROR_DETECTION_EN 0x02
#define ERROR_DETECTION_DIS 0x00

/* Code RAM addresses use by header */
/* Put FW at the begin of CODE RAM */
#define FW_START_ADDR CONFIG_PROGRAM_MEMORY_BASE

/* TODO: It will be filled automatically by ECST */
/* The entry point of reset handler (filled by ECST tool)*/
#define FW_ENTRY_ADDR 0x100A8169

/* Error detection addresses use by header (A offset relative to flash image) */
#define ERRCHK_START_ADDR 0x0
#define ERRCHK_END_ADDR 0x0

/* Firmware Size -> Booter loads RO region after hard reset (16 bytes aligned)*/
#define FW_SIZE CONFIG_RO_SIZE

/**
 * FW Header used by NPCX Booter
 * Documentation: go/cros-fp-npcx99fp-bootloader
 */
struct __packed fw_header_t {
	uint32_t anchor; /* A constant used to verify FW header       */
	uint16_t ext_anchor; /* Enable/disable firmware header CRC check  */
	uint8_t spi_max_freq; /* Spi maximum allowable clock frequency     */
	uint8_t spi_read_mode; /* Spi read mode used for firmware loading   */
	uint8_t cfg_err_detect; /* FW load error detection configuration     */
	uint32_t fw_load_addr; /* Firmware load start address               */
	uint32_t fw_entry; /* Firmware entry point                      */
	uint32_t err_detect_start_addr; /* FW error detect start address      */
	uint32_t err_detect_end_addr; /* FW error detect end address        */
	uint32_t fw_length; /* Firmware length in bytes                  */
	uint8_t flash_size; /* Indicate SPI flash size                   */
	uint8_t reserved[26]; /* Reserved bytes                            */
	uint32_t sig_header; /* The CRC signature of the firmware header  */
	uint32_t sig_fw_image; /* The CRC or Checksum of the firmware image */
} __aligned(1);
BUILD_ASSERT(sizeof(struct fw_header_t) == 64);

__keep __attribute__((section(".header")))
const struct fw_header_t fw_header = {
	/* 00 */ SIG_FW_EC,
	/* 04 */ 0x54E1, /* Header CRC check Enable/Disable -> AB1Eh/54E1h    */
	/* 06 */ 0x04, /* 20/25/33/40/50 MHz -> 00/01/02/03/04h             */
	/* 07 */ 0x03, /* Normal/Fast/Rev/D_IO/Q_IO Mode -> 00/01/02/03/04h */
	/* 08 */ 0x00, /* Disable CRC check functionality */
	/* 09 */ FW_START_ADDR,
	/* 0D */ FW_ENTRY_ADDR, /* Filling by ECST tool with -usearmrst option
				 */
	/* 11 */ ERRCHK_START_ADDR,
	/* 15 */ ERRCHK_END_ADDR,
	/* 19 */ FW_SIZE, /* Filling by ECST tool */
	/* 1D */ 0x0F, /* Flash Size 1/2/4/8/16 Mbytes -> 01/03/07/0F/1Fh   */
	/* 1E-3F Other fields are filled by ECST tool or reserved         */
};
