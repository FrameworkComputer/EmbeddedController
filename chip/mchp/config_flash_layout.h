/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_FLASH_LAYOUT_H
#define __CROS_EC_CONFIG_FLASH_LAYOUT_H

/*
 * mec17xx flash layout:
 * - Non memory-mapped, external SPI.
 * - RW image at the beginning of writable region.
 * - Bootloader at the beginning of protected region, followed by RO image.
 * - Loader + (RO | RW) loaded into program memory.
 */

/* Non-memmapped, external SPI */
#define CONFIG_EXTERNAL_STORAGE
#undef  CONFIG_MAPPED_STORAGE
#undef  CONFIG_FLASH_PSTATE
#define CONFIG_SPI_FLASH

/*
 * MEC17xx BootROM uses two 4-byte TAG's at SPI offset 0x0 and 0x04.
 * One valid TAG must be present.
 * TAG's point to a Header which must be located on a 256 byte
 * boundary anywhere in the flash (24-bit addressing).
 * Locate BootROM load Header + LFW + EC_RO at start of second
 * 4KB sector (offset 0x1000).
 * Locate BootROM load Header + EC_RW at start of second half of
 * SPI flash.
 * LFW size is 4KB
 * EC_RO and EC_RW padded sizes from the build are 188KB each.
 * Storage size is 1/2 flash size.
 */
#define CONFIG_EC_PROTECTED_STORAGE_OFF	0
/* Lower 256KB of flash is protected region */
#define CONFIG_EC_PROTECTED_STORAGE_SIZE 0x40000
/* Writable storage for EC_RW starts at 256KB */
#define CONFIG_EC_WRITABLE_STORAGE_OFF 0x40000
/* Writeable storage is 256KB */
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  0x40000


/* Loader resides at the beginning of program memory */
#define CONFIG_LOADER_MEM_OFF		0
#define CONFIG_LOADER_SIZE		0x1000

/* Write protect Loader and RO Image */
#define CONFIG_WP_STORAGE_OFF		CONFIG_EC_PROTECTED_STORAGE_OFF
/*
 * Write protect LFW + EC_RO
 */
#define CONFIG_WP_STORAGE_SIZE		CONFIG_EC_PROTECTED_STORAGE_SIZE

/*
 * RO / RW images follow the loader in program memory. Either RO or RW
 * image will be loaded -- both cannot be loaded at the same time.
 */
#define CONFIG_RO_MEM_OFF		(CONFIG_LOADER_MEM_OFF + \
					CONFIG_LOADER_SIZE)
/*
 * Total SRAM and the amount allocated for data are specified
 * by CONFIG_MEC_SRAM_SIZE and CONFIG_RAM_SIZE in config_chip.h
 * The little firmware (lfw) loader is resident in first 4KB of Code SRAM.
 * EC_RO/RW size = Total SRAM - Data SRAM - LFW size.
 * !!! EC_RO/RW size MUST be a multiple of flash erase block size.
 * defined by CONFIG_FLASH_ERASE_SIZE in chip/config_chip.h
 * and must be located on a erase block boundary. !!!
 */
#define CONFIG_RO_SIZE			(CONFIG_MEC_SRAM_SIZE - \
					CONFIG_RAM_SIZE - CONFIG_LOADER_SIZE)
#define CONFIG_RW_MEM_OFF		CONFIG_RO_MEM_OFF
/*
 * NOTE: CONFIG_RW_SIZE is passed to chip/mchp/util/pack_ec.py
 */
#define CONFIG_RW_SIZE			CONFIG_RO_SIZE

/*
 * WP region consists of first half of SPI containing TAGs at beginning
 * of SPI flash and header + binary(LFW+EC_RO) an offset aligned on
 * a 256 byte boundary.
 * NOTE: Changing CONFIG_BOOT_HEADER_STORAGE_OFF requires changing
 * parameter --payload_offset of pack_ec.py in build.mk!
 * Two 4-byte TAG's exist at offset 0 and 4 in the SPI flash device.
 * We only use first TAG pointing to LFW + EC_RO.
 * Header size is 128 bytes. Firmware binary is located immediately
 * after the header.
 * Second half of SPI flash contains:
 * Header(128 bytes) + EC_RW
 * EC flash erase/write commands check alginment base on
 * CONFIG_FLASH_ERASE_SIZE defined in config_chip.h
 * NOTE: EC_RO and EC_RW must start at CONFIG_FLASH_ERASE_SIZE or
 * greater aligned boundaries.
 */
#define CONFIG_BOOT_HEADER_STORAGE_OFF		0x1000
#define CONFIG_RW_BOOT_HEADER_STORAGE_OFF	0

#if defined(CHIP_FAMILY_MEC17XX)
#define CONFIG_BOOT_HEADER_STORAGE_SIZE		0x80
#elif defined(CHIP_FAMILY_MEC152X)
#define CONFIG_BOOT_HEADER_STORAGE_SIZE		0x140
#else 
#error "BUILD ERROR: CHIP_FAMILY not defined!"
#endif 
#define CONFIG_RW_BOOT_HEADER_STORAGE_SIZE	0

/* Loader / lfw image immediately follows the boot header on SPI */
#define CONFIG_LOADER_STORAGE_OFF	(CONFIG_BOOT_HEADER_STORAGE_OFF + \
					CONFIG_BOOT_HEADER_STORAGE_SIZE)

/* RO image immediately follows the loader image */
#define CONFIG_RO_STORAGE_OFF		(CONFIG_LOADER_STORAGE_OFF + \
					CONFIG_LOADER_SIZE)

/*
 * RW image starts at offset 0 of second half of SPI.
 * RW Header not needed.
 */
#define CONFIG_RW_STORAGE_OFF		(CONFIG_RW_BOOT_HEADER_STORAGE_OFF + \
					CONFIG_RW_BOOT_HEADER_STORAGE_SIZE)


#endif /* __CROS_EC_CONFIG_FLASH_LAYOUT_H */
