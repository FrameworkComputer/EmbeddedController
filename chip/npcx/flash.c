/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "flash.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "task.h"
#include "watchdog.h"
#include "console.h"
#include "hwtimer_chip.h"

int all_protected; /* Has all-flash protection been requested? */
int addr_prot_start;
int addr_prot_length;
uint8_t flag_prot_inconsistent;

#define FLASH_ABORT_TIMEOUT     10000

#ifdef CONFIG_EXTERNAL_STORAGE
#define TRISTATE_FLASH(x)
#else
#define TRISTATE_FLASH(x) flash_tristate(x)
#endif
/*****************************************************************************/
/* flash internal functions */
void flash_pinmux(int enable)
{
	/* Select pin-mux for FIU*/
	UPDATE_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_NO_F_SPI, !enable);

	/* CS0/1 pinmux */
	if (enable) {
#if (FIU_CHIP_SELECT == 1)
		SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_F_SPI_CS1_1);
#elif (FIU_CHIP_SELECT == 2)
		SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_F_SPI_CS1_2);
#endif
	} else {
		CLEAR_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_F_SPI_CS1_1);
		CLEAR_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_F_SPI_CS1_2);
	}
}

void flash_tristate(int enable)
{
	/* Enable/Disable FIU pins to tri-state */
	UPDATE_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS, enable);
}

void flash_execute_cmd(uint8_t code, uint8_t cts)
{
	/* set UMA_CODE */
	NPCX_UMA_CODE = code;
	/* execute UMA flash transaction */
	NPCX_UMA_CTS  = cts;
	while (IS_BIT_SET(NPCX_UMA_CTS, NPCX_UMA_CTS_EXEC_DONE))
		;
}

void flash_cs_level(int level)
{
	/* Set chip select to high/low level */
	UPDATE_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_SW_CS1, level);
}

static int flash_wait_ready(int timeout)
{
	uint8_t mask = SPI_FLASH_SR1_BUSY;

	if (timeout <= 0)
		return EC_ERROR_INVAL;

	/* Chip Select down. */
	flash_cs_level(0);
	/* Command for Read status register */
	flash_execute_cmd(CMD_READ_STATUS_REG, MASK_CMD_ONLY);
	while (timeout > 0) {
		/* Read status register */
		NPCX_UMA_CTS  = MASK_RD_1BYTE;
		while (IS_BIT_SET(NPCX_UMA_CTS, NPCX_UMA_CTS_EXEC_DONE))
			;
		/* Busy bit is clear */
		if ((NPCX_UMA_DB0 & mask) == 0)
			break;
		if (--timeout > 0)
			msleep(1);
	}; /* Wait for Busy clear */

	/* Chip Select high. */
	flash_cs_level(1);

	if (timeout == 0)
		return EC_ERROR_TIMEOUT;

	return EC_SUCCESS;
}

int flash_write_enable(void)
{
	uint8_t mask = SPI_FLASH_SR1_WEL;
	int rv;
	/* Wait for previous operation to complete */
	rv = flash_wait_ready(FLASH_ABORT_TIMEOUT);
	if (rv)
		return rv;

	/* Write enable command */
	flash_execute_cmd(CMD_WRITE_EN, MASK_CMD_ONLY);

	/* Wait for flash is not busy */
	rv = flash_wait_ready(FLASH_ABORT_TIMEOUT);
	if (rv)
		return rv;

	if (NPCX_UMA_DB0 & mask)
		return EC_SUCCESS;
	else
		return EC_ERROR_BUSY;
}

void flash_set_address(uint32_t dest_addr)
{
	uint8_t *addr = (uint8_t *)&dest_addr;
	/* Write address */
	NPCX_UMA_AB2 = addr[2];
	NPCX_UMA_AB1 = addr[1];
	NPCX_UMA_AB0 = addr[0];
}

uint8_t flash_get_status1(void)
{
	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Read status register1 */
	flash_execute_cmd(CMD_READ_STATUS_REG, MASK_CMD_RD_1BYTE);
	/* Enable tri-state */
	TRISTATE_FLASH(1);
	return NPCX_UMA_DB0;
}

uint8_t flash_get_status2(void)
{
	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Read status register2 */
	flash_execute_cmd(CMD_READ_STATUS_REG2, MASK_CMD_RD_1BYTE);
	/* Enable tri-state */
	TRISTATE_FLASH(1);
	return NPCX_UMA_DB0;
}

/*****************************************************************************/
/* flash protection functions */
/* Use a copy function of spi_flash.c in flash driver */
/**
 * Computes block write protection range from registers
 * Returns start == len == 0 for no protection
 *
 * @param sr1 Status register 1
 * @param sr2 Status register 2
 * @param start Output pointer for protection start offset
 * @param len Output pointer for protection length
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
static int reg_to_protect(uint8_t sr1, uint8_t sr2, unsigned int *start,
		unsigned int *len)
{
	int blocks;
	int size;
	uint8_t cmp;
	uint8_t sec;
	uint8_t tb;
	uint8_t bp;

	/* Determine flags */
	cmp = (sr2 & SPI_FLASH_SR2_CMP) ? 1 : 0;
	sec = (sr1 & SPI_FLASH_SR1_SEC) ? 1 : 0;
	tb = (sr1 & SPI_FLASH_SR1_TB) ? 1 : 0;
	bp = (sr1 & (SPI_FLASH_SR1_BP2 | SPI_FLASH_SR1_BP1 | SPI_FLASH_SR1_BP0))
				 >> 2;

	/* Bad pointers or invalid data */
	if (!start || !len || sr1 == -1 || sr2 == -1)
		return EC_ERROR_INVAL;

	/* Not defined by datasheet */
	if (sec && bp == 6)
		return EC_ERROR_INVAL;

	/* Determine granularity (4kb sector or 64kb block) */
	/* Computation using 2 * 1024 is correct */
	size = sec ? (2 * 1024) : (64 * 1024);

	/* Determine number of blocks */
	/* Equivalent to pow(2, bp) with pow(2, 0) = 0 */
	blocks = bp ? (1 << bp) : 0;
	/* Datasheet specifies don't care for BP == 4, BP == 5 */
	if (sec && bp == 5)
		blocks = (1 << 4);

	/* Determine number of bytes */
	*len = size * blocks;

	/* Determine bottom/top of memory to protect */
	*start = tb ? 0 : (CONFIG_FLASH_SIZE - *len);

	/* Reverse computations if complement set */
	if (cmp) {
		*start = *start + *len;
		*len = CONFIG_FLASH_SIZE - *len;
	}

	/*
	 * If SRP0 is not set, flash is not protected because status register
	 * can be rewritten.
	 */
	if (!(sr1 & SPI_FLASH_SR1_SRP0)) {
		/* Set protection inconsistent if len != 0*/
		if (*len != 0)
			flag_prot_inconsistent = 1;
		*start = *len = 0;
		return EC_SUCCESS;
	}
	/* Flag for checking protection inconsistent */
	flag_prot_inconsistent = 0;

	return EC_SUCCESS;
}

/**
 * Computes block write protection registers from range
 *
 * @param start Desired protection start offset
 * @param len Desired protection length
 * @param sr1 Output pointer for status register 1
 * @param sr2 Output pointer for status register 2
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
static int protect_to_reg(unsigned int start, unsigned int len,
		uint8_t *sr1, uint8_t *sr2)
{
	char cmp = 0;
	char sec = 0;
	char tb = 0;
	char bp = 0;
	int blocks;
	int size;

	/* Bad pointers */
	if (!sr1 || !sr2 || *sr1 == -1 || *sr2 == -1)
		return EC_ERROR_INVAL;

	/* Invalid data */
	if ((start && !len) || start + len > CONFIG_FLASH_SIZE)
		return EC_ERROR_INVAL;

	/* Set complement bit based on whether length is power of 2 */
	if ((len & (len - 1)) != 0) {
		cmp = 1;
		start = start + len;
		len = CONFIG_FLASH_SIZE - len;
	}

	/* Set bottom/top bit based on start address */
	/* Do not set if len == 0 or len == CONFIG_FLASH_SIZE */
	if (!start && (len % CONFIG_FLASH_SIZE))
		tb = 1;

	/* Set sector bit and determine block length based on protect length */
	if (len == 0 || len >= 128 * 1024) {
		sec = 0;
		size = 64 * 1024;
	} else if (len >= 4 * 1024 && len <= 32 * 1024) {
		sec = 1;
		size = 2 * 1024;
	} else
		return EC_ERROR_INVAL;

	/* Determine number of blocks */
	if (len % size != 0)
		return EC_ERROR_INVAL;
	blocks = len / size;

	/* Determine bp = log2(blocks) with log2(0) = 0 */
	bp = blocks ? __fls(blocks) : 0;

	/* Clear bits */
	*sr1 &= ~(SPI_FLASH_SR1_SEC | SPI_FLASH_SR1_TB
		| SPI_FLASH_SR1_BP2 | SPI_FLASH_SR1_BP1 | SPI_FLASH_SR1_BP0);
	*sr2 &= ~SPI_FLASH_SR2_CMP;

	/* Set bits */
	*sr1 |= (sec ? SPI_FLASH_SR1_SEC : 0) | (tb ? SPI_FLASH_SR1_TB : 0)
		| (bp << 2);
	*sr2 |= (cmp ? SPI_FLASH_SR2_CMP : 0);

	/* Set SRP0 so status register can't be changed */
	*sr1 |= SPI_FLASH_SR1_SRP0;

	return EC_SUCCESS;
}

int flash_set_status_for_prot(int reg1, int reg2)
{
	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Enable write */
	flash_write_enable();

	NPCX_UMA_DB0 = reg1;
	NPCX_UMA_DB1 = reg2;

	/* Write status register 1/2 */
	flash_execute_cmd(CMD_WRITE_STATUS_REG, MASK_CMD_WR_2BYTE);
	/* Enable tri-state */
	TRISTATE_FLASH(1);

	reg_to_protect(reg1, reg2, &addr_prot_start, &addr_prot_length);

	return EC_SUCCESS;
}

int flash_check_prot_range(unsigned int offset, unsigned int bytes)
{
	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE)
		return EC_ERROR_INVAL;
	/* Check if ranges overlap */
	if (MAX(addr_prot_start, offset) < MIN(addr_prot_start +
		addr_prot_length, offset + bytes))
		return EC_ERROR_ACCESS_DENIED;

	return EC_SUCCESS;
}

int flash_check_prot_reg(unsigned int offset, unsigned int bytes)
{
	unsigned int start;
	unsigned int len;
	uint8_t sr1 = 0, sr2 = 0;
	int rv = EC_SUCCESS;

	sr1 = flash_get_status1();
	sr2 = flash_get_status2();

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE)
		return EC_ERROR_INVAL;

	/* Compute current protect range */
	rv = reg_to_protect(sr1, sr2, &start, &len);
	if (rv)
		return rv;

	/* Check if ranges overlap */
	if (MAX(start, offset) < MIN(start + len, offset + bytes))
		return EC_ERROR_ACCESS_DENIED;

	return EC_SUCCESS;

}

int flash_write_prot_reg(unsigned int offset, unsigned int bytes)
{
	int rv;
	uint8_t sr1 = flash_get_status1();
	uint8_t sr2 = flash_get_status2();

	/* Invalid values */
	if (offset + bytes > CONFIG_FLASH_SIZE)
		return EC_ERROR_INVAL;

	/* Compute desired protect range */
	rv = protect_to_reg(offset, bytes, &sr1, &sr2);
	if (rv)
		return rv;

	return flash_set_status_for_prot(sr1, sr2);
}

void flash_burst_write(unsigned int dest_addr, unsigned int bytes,
		const char *data)
{
	unsigned int i;
	/* Chip Select down */
	flash_cs_level(0);
	/* Set write address */
	flash_set_address(dest_addr);
	/* Start programming */
	flash_execute_cmd(CMD_FLASH_PROGRAM, MASK_CMD_WR_ADR);
	for (i = 0; i < bytes; i++) {
		flash_execute_cmd(*data, MASK_CMD_WR_ONLY);
		data++;
	}
	/* Chip Select up */
	flash_cs_level(1);
}

static int flash_program_bytes(uint32_t offset, uint32_t bytes,
	const uint8_t const *data)
{
	int write_size;
	int rv;

	while (bytes > 0) {
		/* Write length can not go beyond the end of the flash page */
		write_size = MIN(bytes, CONFIG_FLASH_WRITE_IDEAL_SIZE -
		(offset & (CONFIG_FLASH_WRITE_IDEAL_SIZE - 1)));

		/* Enable write */
		rv = flash_write_enable();
		if (rv)
			return rv;

		/* Burst UMA transaction */
		flash_burst_write(offset, write_size, data);

		/* Wait write completed */
		rv = flash_wait_ready(FLASH_ABORT_TIMEOUT);
		if (rv)
			return rv;

		data   += write_size;
		offset += write_size;
		bytes  -= write_size;
	}

	return rv;
}

int flash_uma_lock(int enable)
{
	UPDATE_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK, enable);
	return EC_SUCCESS;
}

int flash_spi_sel_lock(int enable)
{
	/*
	 * F_SPI_QUAD, F_SPI_CS1_1/2, F_SPI_TRIS become read-only
	 * if this bit is set
	 */
	UPDATE_BIT(NPCX_DEV_CTL4, NPCX_DEV_CTL4_F_SPI_SLLK, enable);
	return IS_BIT_SET(NPCX_DEV_CTL4, NPCX_DEV_CTL4_F_SPI_SLLK);
}

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_read(int offset, int size, char *data)
{
	int dest_addr = offset;
	uint32_t idx;

	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Chip Select down. */
	flash_cs_level(0);

	/* Set read address */
	flash_set_address(dest_addr);
	/* Start fast read - 1110 1001 - EXEC, WR, CMD, ADDR */
	flash_execute_cmd(CMD_FAST_READ, MASK_CMD_ADR_WR);

	/* Burst read transaction */
	for (idx = 0; idx < size; idx++) {
		/* 1101 0101 - EXEC, RD, NO CMD, NO ADDR, 4 bytes */
		NPCX_UMA_CTS  = MASK_RD_1BYTE;
		/* wait for UMA to complete */
		while (IS_BIT_SET(NPCX_UMA_CTS, EXEC_DONE))
			;
		/* Get read transaction results*/
		data[idx] = NPCX_UMA_DB0;
	}

	/* Chip Select up */
	flash_cs_level(1);
	/* Enable tri-state */
	TRISTATE_FLASH(1);
	return EC_SUCCESS;
}

int flash_physical_read_image_size(int offset, int size)
{
	int dest_addr = offset;
	uint8_t		temp;
	uint32_t	idx;
	uint32_t	image_size = 0;

	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Chip Select down. */
	flash_cs_level(0);

	/* Set read address */
	flash_set_address(dest_addr);
	/* Start fast read - 1110 1001 - EXEC, WR, CMD, ADDR */
	flash_execute_cmd(CMD_FAST_READ, MASK_CMD_ADR_WR);

	/* Burst read transaction */
	for (idx = 0; idx < size; idx++) {
		/* 1101 0101 - EXEC, RD, NO CMD, NO ADDR, 4 bytes */
		NPCX_UMA_CTS  = MASK_RD_1BYTE;
		/* wait for UMA to complete */
		while (IS_BIT_SET(NPCX_UMA_CTS, EXEC_DONE))
			;
		/* Find eof of image */
		temp = NPCX_UMA_DB0;
		if (temp == 0xea)
			image_size = idx;
	}

	/* Chip Select up */
	flash_cs_level(1);
	/* Enable tri-state */
	TRISTATE_FLASH(1);
	return image_size;
}

int flash_physical_is_erased(uint32_t offset, int size)
{
	int dest_addr = offset;
	uint32_t idx;
	uint8_t temp;

	/* Chip Select down. */
	flash_cs_level(0);

	/* Set read address */
	flash_set_address(dest_addr);
	/* Start fast read -1110 1001 - EXEC, WR, CMD, ADDR */
	flash_execute_cmd(CMD_FAST_READ, MASK_CMD_ADR_WR);

	/* Burst read transaction */
	for (idx = 0; idx < size; idx++) {
		/* 1101 0101 - EXEC, RD, NO CMD, NO ADDR, 4 bytes */
		NPCX_UMA_CTS  = MASK_RD_1BYTE;
		/* Wait for UMA to complete */
		while (IS_BIT_SET(NPCX_UMA_CTS, EXEC_DONE))
			;
		/* Get read transaction results */
		temp = NPCX_UMA_DB0;
		if (temp != 0xFF)
			break;
	}

	/* Chip Select up */
	flash_cs_level(1);

	if (idx == size)
		return 1;
	else
		return 0;
}

int flash_physical_write(int offset, int size, const char *data)
{
	int dest_addr = offset;
	int write_len;
	int rv;

	/* Fail if offset, size, and data aren't at least word-aligned */
	if ((offset | size
	     | (uint32_t)(uintptr_t)data) & (CONFIG_FLASH_WRITE_SIZE - 1))
		return EC_ERROR_INVAL;

	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* Disable tri-state */
	TRISTATE_FLASH(0);

	while (size > 0) {
		/* First write multiples of 256, then (size % 256) last */
		write_len = ((size % CONFIG_FLASH_WRITE_IDEAL_SIZE) == size) ?
					size : CONFIG_FLASH_WRITE_IDEAL_SIZE;

		/* check protection */
		if (flash_check_prot_range(dest_addr, write_len))
			return EC_ERROR_ACCESS_DENIED;

		rv = flash_program_bytes(dest_addr, write_len, data);
		if (rv)
			return rv;

		data      += write_len;
		dest_addr += write_len;
		size      -= write_len;
	}

	/* Enable tri-state */
	TRISTATE_FLASH(1);
	return rv;
}

int flash_physical_erase(int offset, int size)
{
	int rv = EC_SUCCESS;
	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* Disable tri-state */
	TRISTATE_FLASH(0);

	/* Alignment has been checked in upper layer */
	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE,
		offset += CONFIG_FLASH_ERASE_SIZE) {

		/* Do nothing if already erased */
		if (flash_is_erased(offset, CONFIG_FLASH_ERASE_SIZE))
			continue;

		/* check protection */
		if (flash_check_prot_range(offset, CONFIG_FLASH_ERASE_SIZE))
			return EC_ERROR_ACCESS_DENIED;

		/*
		 * Reload the watchdog timer, so that erasing many flash pages
		 * doesn't cause a watchdog reset.  May not need this now that
		 * we're using msleep() below.
		 */
		watchdog_reload();

		/* Enable write */
		rv = flash_write_enable();
		if (rv)
			return rv;

		/* Set erase address */
		flash_set_address(offset);
		/* Start erase */
		flash_execute_cmd(CMD_SECTOR_ERASE, MASK_CMD_ADR);

		/* Wait erase completed */
		rv = flash_wait_ready(FLASH_ABORT_TIMEOUT);
		if (rv)
			return rv;
	}

	/* Enable tri-state */
	TRISTATE_FLASH(1);
	return rv;
}

int flash_physical_get_protect(int bank)
{
	uint32_t addr = bank * CONFIG_FLASH_BANK_SIZE;
	/* All UMA transaction is locked means all banks are protected */
	if (IS_BIT_SET(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK))
		return EC_ERROR_ACCESS_DENIED;

	return flash_check_prot_reg(addr, CONFIG_FLASH_BANK_SIZE);
}

uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	/* Check if WP region is protected in status register */
	if (flash_check_prot_reg(WP_BANK_OFFSET*CONFIG_FLASH_BANK_SIZE,
				 WP_BANK_COUNT*CONFIG_FLASH_BANK_SIZE))
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * TODO: If status register protects a range, but SRP0 is not set,
	 * flags should indicate EC_FLASH_PROTECT_ERROR_INCONSISTENT.
	 */
	if (flag_prot_inconsistent)
		flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	/* Read all-protected state from our shadow copy */
	if (all_protected)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	return flags;
}

int flash_physical_protect_now(int all)
{
	if (all) {
		all_protected = 1;
		/*
		 * Set UMA_LOCK bit for locking all UMA transaction.
		 * But we still can read directly from flash mapping address
		 */
		flash_uma_lock(1);
	} else {
		all_protected = 0;
		/* Unlocking all UMA transaction */
		flash_uma_lock(0);
	}
	/* TODO: if all, disable SPI interface */

	return EC_SUCCESS;
}


int flash_physical_protect_at_boot(enum flash_wp_range range)
{
	switch (range) {
	case FLASH_WP_NONE:
		/* Unlock UMA transactions */
		if (IS_BIT_SET(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK))
			CLEAR_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK);
		/* Clear protection bits in status register */
		return flash_set_status_for_prot(0, 0);
	case FLASH_WP_RO:
		/* Unlock UMA transactions */
		if (IS_BIT_SET(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK))
			CLEAR_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK);
		/* Protect read-only */
		return flash_write_prot_reg(
			    WP_BANK_OFFSET*CONFIG_FLASH_BANK_SIZE,
			    WP_BANK_COUNT*CONFIG_FLASH_BANK_SIZE);
	case FLASH_WP_ALL:
		/* Protect all */
		/*
		 * Set UMA_LOCK bit for locking all UMA transaction.
		 * But we still can read directly from flash mapping address
		 */
		return flash_uma_lock(1);
	default:
		return EC_ERROR_INVAL;
	}
}

uint32_t flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT |
			EC_FLASH_PROTECT_RO_NOW |
			EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * If entire flash isn't protected at this boot, it can be enabled if
	 * the WP GPIO is asserted.
	 */
	if (!(cur_flags & EC_FLASH_PROTECT_ALL_NOW) &&
			(cur_flags & EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_NOW;

	return ret;
}

/*****************************************************************************/
/* High-level APIs */

int flash_pre_init(void)
{
	/* Enable FIU interface */
	flash_pinmux(1);

#ifdef CONFIG_EXTERNAL_STORAGE
	/* Disable tristate all the time */
	CLEAR_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS);
#endif

		return EC_SUCCESS;
}

void flash_lock_mapped_storage(int lock)
{
	/*
	 * TODO(crosbug.com/p/55781): Add mutex to ensure no conflict between
	 * mapped read and regular flash ops.
	 */
}
/*****************************************************************************/
/* Console commands */

static int command_flash_spi_sel_lock(int argc, char **argv)
{
	int ena;

	if (argc > 1) {
		if (!parse_bool(argv[1], &ena))
			return EC_ERROR_PARAM1;
		ena = flash_spi_sel_lock(ena);
		ccprintf("Enabled: %d\n", ena);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flash_spi_lock, command_flash_spi_sel_lock,
			"[0 | 1]",
			"Lock spi flash interface selection");

static int command_flash_tristate(int argc, char **argv)
{
	int ena;

	if (argc > 1) {
		if (!parse_bool(argv[1], &ena))
			return EC_ERROR_PARAM1;
		flash_tristate(ena);
		ccprintf("Enabled: %d\n", ena);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flash_tristate, command_flash_tristate,
			"[0 | 1]",
			"Tristate spi flash pins");

