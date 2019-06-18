/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ROM_CHIP_H_
#define __CROS_EC_ROM_CHIP_H_

/******************************************************************************/
/*
 * Enumerations of ROM api functions
 */
enum API_SIGN_OPTIONS_T {
	SIGN_NO_CHECK = 0,
	SIGN_CRC_CHECK = 1,
};

enum API_RETURN_STATUS_T {
	/* Successful download */
	API_RET_STATUS_OK = 0,
	/* Address is outside of flash or not 4 bytes aligned. */
	API_RET_STATUS_INVALID_SRC_ADDR = 1,
	/* Address is outside of RAM or not 4 bytes aligned. */
	API_RET_STATUS_INVALID_DST_ADDR = 2,
	/* Size is 0 or not 4 bytes aligned. */
	API_RET_STATUS_INVALID_SIZE = 3,
	/* Flash Address + Size is out of flash. */
	API_RET_STATUS_INVALID_SIZE_OUT_OF_FLASH = 4,
	/* RAM Address + Size is out of RAM. */
	API_RET_STATUS_INVALID_SIZE_OUT_OF_RAM = 5,
	/* Wrong sign option. */
	API_RET_STATUS_INVALID_SIGN = 6,
	/* Error during Code copy. */
	API_RET_STATUS_COPY_FAILED = 7,
	/* Execution Address is outside of RAM */
	API_RET_STATUS_INVALID_EXE_ADDR = 8,
	/* Bad CRC value */
	API_RET_STATUS_INVALID_SIGNATURE = 9,
};

/******************************************************************************/
/*
 * Macro functions of ROM api functions
 */
#define ADDR_DOWNLOAD_FROM_FLASH (*(volatile uint32_t *) 0x40)
#define download_from_flash(src_offset, dest_addr, size, sign, exe_addr, \
	status) \
	(((download_from_flash_ptr) ADDR_DOWNLOAD_FROM_FLASH) \
	(src_offset, dest_addr, size, sign, exe_addr, status))

/******************************************************************************/
/*
 * Declarations of ROM api functions
 */
typedef void (*download_from_flash_ptr) (
	uint32_t src_offset, /* The offset of the data to be downloaded */
	uint32_t dest_addr,  /* The address of the downloaded data in the RAM*/
	uint32_t size,       /* Number of bytes to download */
	enum API_SIGN_OPTIONS_T sign, /* Need CRC check or not */
	uint32_t exe_addr, /* jump to this address after download if not zero */
	enum API_RETURN_STATUS_T *status /* Status fo download */
);



#endif /* __CROS_EC_ROM_CHIP_H_ */
