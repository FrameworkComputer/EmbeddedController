/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ROM_CHIP_H_
#define __CROS_EC_ROM_CHIP_H_

#include "util.h"

/******************************************************************************/
/*
 * Enumerations of ROM API functions
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
	/* OTP API succeeded */
	API_RET_OTP_STATUS_OK = 0xA5A5,
	/* OTP API failed */
	API_RET_OTP_STATUS_FAIL = 0x5A5A
};

/******************************************************************************/
/*
 * Declarations of ROM API functions
 */

/*
 * src_offset -  The offset of the data to be downloaded
 * dest_addr - The address of the downloaded data in the RAM
 * size - Number of bytes to download
 * sign - Need CRC check or not
 * exe_addr - jump to this address after download if not zero
 * status - Status of download
 */
typedef void (*download_from_flash_ptr)(uint32_t src_offset, uint32_t dest_addr,
					uint32_t size,
					enum API_SIGN_OPTIONS_T sign,
					uint32_t exe_addr,
					enum API_RETURN_STATUS_T *ec_status);

/* true: OTP hardware on, false: OTP hardware off */
typedef enum API_RETURN_STATUS_T (*otpi_power_ptr)(bool on);

/*
 * address - OTP address to read from
 * data - pointer to 8-bit variable to store read data
 */
typedef enum API_RETURN_STATUS_T (*otpi_read_ptr)(uint32_t address,
						  uint8_t *data);

/*
 * address - OTP address to write to
 * data -  8-bit data value
 */
typedef enum API_RETURN_STATUS_T (*otpi_write_ptr)(uint32_t address,
						   uint8_t data);

/*
 * address - OTP address to protect, 16B aligned
 * size - Number of bytes to be protected, 16B aligned
 */
typedef enum API_RETURN_STATUS_T (*otpi_write_prot_ptr)(uint32_t address,
							uint32_t size);

/******************************************************************************/
/*
 * Inline functions for ROM APIs
 */
static const volatile uint32_t *ADDR_DOWNLOAD_FROM_FLASH = (uint32_t *)0x40;
static inline void download_from_flash(uint32_t src_offset, uint32_t dest_addr,
				       uint32_t size,
				       enum API_SIGN_OPTIONS_T sign,
				       uint32_t exe_addr,
				       enum API_RETURN_STATUS_T *status)
{
	((download_from_flash_ptr)*ADDR_DOWNLOAD_FROM_FLASH)(
		src_offset, dest_addr, size, sign, exe_addr, status);
}

#ifndef HAS_MOCK_OTPI
static const volatile uint32_t *ADDR_OTPI_POWER = (uint32_t *)0x4C;
static inline enum API_RETURN_STATUS_T otpi_power(bool on)
{
	return ((otpi_power_ptr)*ADDR_OTPI_POWER)(on);
}

static const volatile uint32_t *ADDR_OTPI_READ = (uint32_t *)0x50;

static inline enum API_RETURN_STATUS_T otpi_read(uint32_t address,
						 uint8_t *data)
{
	return ((otpi_read_ptr)*ADDR_OTPI_READ)(address, data);
}

static const volatile uint32_t *ADDR_OTPI_WRITE = (uint32_t *)0x54;
static inline enum API_RETURN_STATUS_T otpi_write(uint32_t address,
						  uint8_t data)
{
	return ((otpi_write_ptr)*ADDR_OTPI_WRITE)(address, data);
}

static const volatile uint32_t *ADDR_OTPI_WRITE_PROTECT = (uint32_t *)0x5C;
static inline enum API_RETURN_STATUS_T otpi_write_protect(uint32_t address,
							  uint32_t size)
{
	return ((otpi_write_prot_ptr)*ADDR_OTPI_WRITE_PROTECT)(address, size);
}
#else
extern enum API_RETURN_STATUS_T otpi_power(bool on);
extern enum API_RETURN_STATUS_T otpi_read(uint32_t address, uint8_t *data);
extern enum API_RETURN_STATUS_T otpi_write(uint32_t address, uint8_t data);
extern enum API_RETURN_STATUS_T otpi_write_protect(uint32_t address,
						   uint32_t size);
#endif

#endif /* __CROS_EC_ROM_CHIP_H_ */
