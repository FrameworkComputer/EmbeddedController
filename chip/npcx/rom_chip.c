/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rom_chip.h"

static const volatile uint32_t *ADDR_DOWNLOAD_FROM_FLASH = (uint32_t *)0x40;
static const volatile uint32_t *ADDR_OTPI_POWER = (uint32_t *)0x4C;
static const volatile uint32_t *ADDR_OTPI_READ = (uint32_t *)0x50;
static const volatile uint32_t *ADDR_OTPI_WRITE = (uint32_t *)0x54;
static const volatile uint32_t *ADDR_OTPI_WRITE_PROTECT = (uint32_t *)0x5C;

typedef void (*download_from_flash_ptr)(uint32_t src_offset, uint32_t dest_addr,
					uint32_t size,
					enum API_SIGN_OPTIONS_T sign,
					uint32_t exe_addr,
					enum API_RETURN_STATUS_T *ec_status);

typedef enum API_RETURN_STATUS_T (*otpi_power_ptr)(bool on);
typedef enum API_RETURN_STATUS_T (*otpi_read_ptr)(uint32_t address,
						  uint8_t *data);
typedef enum API_RETURN_STATUS_T (*otpi_write_ptr)(uint32_t address,
						   uint8_t data);
typedef enum API_RETURN_STATUS_T (*otpi_write_prot_ptr)(uint32_t address,
							uint32_t size);

test_mockable enum API_RETURN_STATUS_T otpi_power(bool on)
{
	return ((otpi_power_ptr)*ADDR_OTPI_POWER)(on);
}

test_mockable enum API_RETURN_STATUS_T otpi_read(uint32_t address,
						 uint8_t *data)
{
	return ((otpi_read_ptr)*ADDR_OTPI_READ)(address, data);
}

test_mockable enum API_RETURN_STATUS_T otpi_write(uint32_t address,
						  uint8_t data)
{
	return ((otpi_write_ptr)*ADDR_OTPI_WRITE)(address, data);
}

test_mockable enum API_RETURN_STATUS_T otpi_write_protect(uint32_t address,
							  uint32_t size)
{
	return ((otpi_write_prot_ptr)*ADDR_OTPI_WRITE_PROTECT)(address, size);
}

void download_from_flash(uint32_t src_offset, uint32_t dest_addr, uint32_t size,
			 enum API_SIGN_OPTIONS_T sign, uint32_t exe_addr,
			 enum API_RETURN_STATUS_T *status)
{
	((download_from_flash_ptr)*ADDR_DOWNLOAD_FROM_FLASH)(
		src_offset, dest_addr, size, sign, exe_addr, status);
}
