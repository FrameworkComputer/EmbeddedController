/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SPI flash interface for Chrome EC */

#ifndef __CROS_EC_SPI_FLASH_H
#define __CROS_EC_SPI_FLASH_H

/* Obtain SPI flash size from JEDEC size */
#define SPI_FLASH_SIZE(x) (1 << (x))

/* SPI flash instructions */
#define SPI_FLASH_WRITE_ENABLE		0x06
#define SPI_FLASH_WRITE_DISABLE		0x04
#define SPI_FLASH_READ_SR1		0x05
#define SPI_FLASH_READ_SR2		0x35
#define SPI_FLASH_WRITE_SR		0x01
#define SPI_FLASH_ERASE_4KB		0x20
#define SPI_FLASH_ERASE_32KB		0x52
#define SPI_FLASH_ERASE_64KB		0xD8
#define SPI_FLASH_ERASE_CHIP		0xC7
#define SPI_FLASH_READ			0x03
#define SPI_FLASH_PAGE_PRGRM		0x02
#define SPI_FLASH_REL_PWRDWN		0xAB
#define SPI_FLASH_MFR_DEV_ID		0x90
#define SPI_FLASH_JEDEC_ID		0x9F
#define SPI_FLASH_UNIQUE_ID		0x4B
#define SPI_FLASH_SFDP			0x44
#define SPI_FLASH_ERASE_SEC_REG		0x44
#define SPI_FLASH_PRGRM_SEC_REG		0x42
#define SPI_FLASH_READ_SEC_REG		0x48
#define SPI_FLASH_ENABLE_RESET		0x66
#define SPI_FLASH_RESET			0x99

/* Maximum single write size (in bytes) for the W25Q64FV SPI flash */
#define SPI_FLASH_MAX_WRITE_SIZE	256

/*
 * Maximum message size (in bytes) for the W25Q64FV SPI flash
 * Instruction (1) + Address (3) + Data (256) = 260
 * Limited by chip maximum input length of write instruction
 */
#define SPI_FLASH_MAX_MESSAGE_SIZE	(SPI_FLASH_MAX_WRITE_SIZE + 4)

/* Maximum single read size in bytes. Limited by size of the message buffer */
#define SPI_FLASH_MAX_READ_SIZE		(SPI_FLASH_MAX_MESSAGE_SIZE - 4)

/* Status register write protect structure */
enum spi_flash_wp {
	SPI_WP_NONE,
	SPI_WP_HARDWARE,
	SPI_WP_POWER_CYCLE,
	SPI_WP_PERMANENT,
};

/**
 * Waits for the chip to finish the current operation. Must be called
 * after erase/erite operations to ensure successive commands are executed.
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
int spi_flash_wait(void);

/**
 * Returns the contents of SPI flash status register 1
 *
 * @return register contents
 */
uint8_t spi_flash_get_status1(void);

/**
 * Returns the contents of SPI flash status register 2
 *
 * @return register contents
 */
uint8_t spi_flash_get_status2(void);

/**
 * Sets the SPI flash status registers (non-volatile bits only)
 * Pass reg2 == -1 to only set reg1.
 *
 * @param reg1 Status register 1
 * @param reg2 Status register 2 (optional)
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
int spi_flash_set_status(int reg1, int reg2);

/**
 * Returns the content of SPI flash
 *
 * @param buf Buffer to write flash contents
 * @param offset Flash offset to start reading from
 * @param bytes Number of bytes to read. Limited by receive buffer to 256.
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
int spi_flash_read(uint8_t *buf, unsigned int offset, unsigned int bytes);

/**
 * Erase SPI flash.
 *
 * @param offset Flash offset to start erasing
 * @param bytes Number of bytes to erase
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
int spi_flash_erase(unsigned int offset, unsigned int bytes);

/**
 * Write to SPI flash. Assumes already erased.
 * Limited to SPI_FLASH_MAX_WRITE_SIZE by chip.
 *
 * @param offset Flash offset to write
 * @param bytes Number of bytes to write
 * @param data Data to write to flash
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
int spi_flash_write(unsigned int offset, unsigned int bytes,
	const uint8_t const *data);

/**
 * Gets the SPI flash JEDEC ID (manufacturer ID, memory type, and capacity)
 *
 * @param dest		Destination buffer; must be 3 bytes long
 * @return EC_SUCCESS or non-zero on error
 */
int spi_flash_get_jedec_id(uint8_t *dest);

/**
 * Gets the SPI flash manufacturer and device ID
 *
 * @param dest		Destination buffer; must be 2 bytes long
 * @return EC_SUCCESS or non-zero on error
 */
int spi_flash_get_mfr_dev_id(uint8_t *dest);

/**
 * Gets the SPI flash unique ID (serial)
 *
 * @param dest		Destination buffer; must be 8 bytes long
 * @return EC_SUCCESS or non-zero on error
 */
int spi_flash_get_unique_id(uint8_t *dest);

/**
 * Check for SPI flash status register write protection.
 * Note that this does not check the hardware WP pin as we might not be
 * able to read the WP pin status.
 *
 * @return enum spi_flash_wp status based on protection
 */
enum spi_flash_wp spi_flash_check_wp(void);

/**
 * Set SPI flash status register write protection
 *
 * @param wp Status register write protection mode
 *
 * @return EC_SUCCESS for no protection, or non-zero if error.
 */
int spi_flash_set_wp(enum spi_flash_wp);

/**
 * Check for SPI flash block write protection
 *
 * @param offset Flash block offset to check
 * @param bytes Flash block length to check
 *
 * @return EC_SUCCESS if no protection, or non-zero if error.
 */
int spi_flash_check_protect(unsigned int offset, unsigned int bytes);

/**
 * Set SPI flash block write protection.
 * If offset == bytes == 0, remove protection.
 *
 * @param offset Flash block offset to protect
 * @param bytes Flash block length to protect
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int spi_flash_set_protect(unsigned int offset, unsigned int bytes);

#endif  /* __CROS_EC_SPI_FLASH_H */
