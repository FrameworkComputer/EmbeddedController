/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "flash.h"
#include "host_command.h"
#include "registers.h"
#include "spi_flash_reg.h"
#include "switch.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "task.h"
#include "watchdog.h"
#include "console.h"
#include "hwtimer_chip.h"

static int all_protected; /* Has all-flash protection been requested? */
static int addr_prot_start;
static int addr_prot_length;
static uint8_t flag_prot_inconsistent;

/* SR regs aren't readable when UMA lock is on, so save a copy */
static uint8_t saved_sr1;
static uint8_t saved_sr2;

#define FLASH_ABORT_TIMEOUT     10000

#ifdef CONFIG_EXTERNAL_STORAGE
#define TRISTATE_FLASH(x)
#else
#define TRISTATE_FLASH(x) flash_tristate(x)
#endif

/* Ensure only one task is accessing flash at a time. */
static struct mutex flash_lock;

/*****************************************************************************/
/* flash internal functions */
static void flash_pinmux(int enable)
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

static void flash_tristate(int enable)
{
	/* Enable/Disable FIU pins to tri-state */
	UPDATE_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS, enable);
}

static void flash_execute_cmd(uint8_t code, uint8_t cts)
{
	/* set UMA_CODE */
	NPCX_UMA_CODE = code;
	/* execute UMA flash transaction */
	NPCX_UMA_CTS  = cts;
	while (IS_BIT_SET(NPCX_UMA_CTS, NPCX_UMA_CTS_EXEC_DONE))
		;
}

static void flash_cs_level(int level)
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

static int flash_write_enable(void)
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

static void flash_set_address(uint32_t dest_addr)
{
	uint8_t *addr = (uint8_t *)&dest_addr;
	/* Write address */
	NPCX_UMA_AB2 = addr[2];
	NPCX_UMA_AB1 = addr[1];
	NPCX_UMA_AB0 = addr[0];
}

static uint8_t flash_get_status1(void)
{
	if (all_protected)
		return saved_sr1;

	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Read status register1 */
	flash_execute_cmd(CMD_READ_STATUS_REG, MASK_CMD_RD_1BYTE);
	/* Enable tri-state */
	TRISTATE_FLASH(1);
	return NPCX_UMA_DB0;
}

static uint8_t flash_get_status2(void)
{
	if (all_protected)
		return saved_sr2;

	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Read status register2 */
	flash_execute_cmd(CMD_READ_STATUS_REG2, MASK_CMD_RD_1BYTE);
	/* Enable tri-state */
	TRISTATE_FLASH(1);
	return NPCX_UMA_DB0;
}

#ifdef CONFIG_HOSTCMD_FLASH_SPI_INFO

void flash_get_mfr_dev_id(uint8_t *dest)
{
	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Read manufacturer and device ID.  Send cmd=0x90 + 24-bit address=0 */
	flash_set_address(0);
	flash_execute_cmd(CMD_READ_MAN_DEV_ID,
			  MASK_CMD_RD_2BYTE | MASK(A_SIZE));
	/* Enable tri-state */
	TRISTATE_FLASH(1);

	dest[0] = NPCX_UMA_DB0;
	dest[1] = NPCX_UMA_DB1;
}

void flash_get_jedec_id(uint8_t *dest)
{
	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Read manufacturer and device ID */
	flash_execute_cmd(CMD_READ_ID, MASK_CMD_RD_3BYTE);
	/* Enable tri-state */
	TRISTATE_FLASH(1);

	dest[0] = NPCX_UMA_DB0;
	dest[1] = NPCX_UMA_DB1;
	dest[2] = NPCX_UMA_DB2;
}

#endif /* CONFIG_HOSTCMD_FLASH_SPI_INFO */

static void flash_uma_lock(int enable)
{
	if (enable && !all_protected) {
		/*
		 * Store SR1 / SR2 for later use since we're about to lock
		 * out all access (including read access) to these regs.
		 */
		saved_sr1 = flash_get_status1();
		saved_sr2 = flash_get_status2();
	}

	all_protected = enable;
	UPDATE_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK, enable);
}

static int flash_set_status_for_prot(int reg1, int reg2)
{
	/*
	 * Writing SR regs will fail if our UMA lock is enabled. If WP
	 * is deasserted then remove the lock and allow the write.
	 */
	if (all_protected) {
		if (flash_get_protect() & EC_FLASH_PROTECT_GPIO_ASSERTED)
			return EC_ERROR_ACCESS_DENIED;
		flash_uma_lock(0);
	}

	/* Lock physical flash operations */
	flash_lock_mapped_storage(1);

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

	/* Unlock physical flash operations */
	flash_lock_mapped_storage(0);

	spi_flash_reg_to_protect(reg1, reg2,
				 &addr_prot_start, &addr_prot_length);

	return EC_SUCCESS;
}

static int flash_check_prot_range(unsigned int offset, unsigned int bytes)
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

static int flash_check_prot_reg(unsigned int offset, unsigned int bytes)
{
	unsigned int start;
	unsigned int len;
	uint8_t sr1, sr2;
	int rv = EC_SUCCESS;

	sr1 = flash_get_status1();
	sr2 = flash_get_status2();

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE)
		return EC_ERROR_INVAL;

	/* Compute current protect range */
	rv = spi_flash_reg_to_protect(sr1, sr2, &start, &len);
	if (rv)
		return rv;

	/* Check if ranges overlap */
	if (MAX(start, offset) < MIN(start + len, offset + bytes))
		return EC_ERROR_ACCESS_DENIED;

	return EC_SUCCESS;

}

static int flash_write_prot_reg(unsigned int offset, unsigned int bytes,
				int hw_protect)
{
	int rv;
	uint8_t sr1 = flash_get_status1();
	uint8_t sr2 = flash_get_status2();

	/* Invalid values */
	if (offset + bytes > CONFIG_FLASH_SIZE)
		return EC_ERROR_INVAL;

	/* Compute desired protect range */
	rv = spi_flash_protect_to_reg(offset, bytes, &sr1, &sr2);
	if (rv)
		return rv;

	if (hw_protect)
		sr1 |= SPI_FLASH_SR1_SRP0;

	return flash_set_status_for_prot(sr1, sr2);
}

static void flash_burst_write(unsigned int dest_addr, unsigned int bytes,
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

static int flash_spi_sel_lock(int enable)
{
	/*
	 * F_SPI_QUAD, F_SPI_CS1_1/2, F_SPI_TRIS become read-only
	 * if this bit is set
	 */
	UPDATE_BIT(NPCX_DEV_CTL4, NPCX_DEV_CTL4_F_SPI_SLLK, enable);
	return IS_BIT_SET(NPCX_DEV_CTL4, NPCX_DEV_CTL4_F_SPI_SLLK);
}

/*****************************************************************************/

int flash_physical_read(int offset, int size, char *data)
{
	int dest_addr = offset;
	uint32_t idx;

	/* Lock physical flash operations */
	flash_lock_mapped_storage(1);

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

	/* Unlock physical flash operations */
	flash_lock_mapped_storage(0);

	return EC_SUCCESS;
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

	/* Lock physical flash operations */
	flash_lock_mapped_storage(1);

	/* Disable tri-state */
	TRISTATE_FLASH(0);

	while (size > 0) {
		/* First write multiples of 256, then (size % 256) last */
		write_len = ((size % CONFIG_FLASH_WRITE_IDEAL_SIZE) == size) ?
					size : CONFIG_FLASH_WRITE_IDEAL_SIZE;

		/* check protection */
		if (flash_check_prot_range(dest_addr, write_len)) {
			rv = EC_ERROR_ACCESS_DENIED;
			break;
		}

		rv = flash_program_bytes(dest_addr, write_len, data);
		if (rv)
			break;

		data      += write_len;
		dest_addr += write_len;
		size      -= write_len;
	}

	/* Enable tri-state */
	TRISTATE_FLASH(1);

	/* Unlock physical flash operations */
	flash_lock_mapped_storage(0);

	return rv;
}

int flash_physical_erase(int offset, int size)
{
	int rv = EC_SUCCESS;
	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* Lock physical flash operations */
	flash_lock_mapped_storage(1);

	/* Disable tri-state */
	TRISTATE_FLASH(0);

	/* Alignment has been checked in upper layer */
	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE,
		offset += CONFIG_FLASH_ERASE_SIZE) {
		/* check protection */
		if (flash_check_prot_range(offset, CONFIG_FLASH_ERASE_SIZE)) {
			rv = EC_ERROR_ACCESS_DENIED;
			break;
		}

		/*
		 * Reload the watchdog timer, so that erasing many flash pages
		 * doesn't cause a watchdog reset.  May not need this now that
		 * we're using msleep() below.
		 */
		watchdog_reload();

		/* Enable write */
		rv = flash_write_enable();
		if (rv)
			break;

		/* Set erase address */
		flash_set_address(offset);
		/* Start erase */
		flash_execute_cmd(CMD_SECTOR_ERASE, MASK_CMD_ADR);

		/* Wait erase completed */
		rv = flash_wait_ready(FLASH_ABORT_TIMEOUT);
		if (rv)
			break;
	}

	/* Enable tri-state */
	TRISTATE_FLASH(1);

	/* Unlock physical flash operations */
	flash_lock_mapped_storage(0);

	return rv;
}

int flash_physical_get_protect(int bank)
{
	uint32_t addr = bank * CONFIG_FLASH_BANK_SIZE;

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
		/*
		 * Set UMA_LOCK bit for locking all UMA transaction.
		 * But we still can read directly from flash mapping address
		 */
		flash_uma_lock(1);
	} else {
		/* TODO: Implement RO "now" protection */
	}

	return EC_SUCCESS;
}


int flash_physical_protect_at_boot(enum flash_wp_range range)
{
	switch (range) {
	case FLASH_WP_NONE:
		/* Clear protection bits in status register */
		return flash_set_status_for_prot(0, 0);
	case FLASH_WP_RO:
		/* Protect read-only */
		return flash_write_prot_reg(CONFIG_WP_STORAGE_OFF,
					    CONFIG_WP_STORAGE_SIZE,
					    1);
	case FLASH_WP_ALL:
		flash_write_prot_reg(CONFIG_WP_STORAGE_OFF,
				     CONFIG_WP_STORAGE_SIZE,
				     1);

		/*
		 * Set UMA_LOCK bit for locking all UMA transaction.
		 * But we still can read directly from flash mapping address
		 */
		flash_uma_lock(1);
		return EC_SUCCESS;
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

	/* Initialize UMA to unlocked */
	flash_uma_lock(0);
	return EC_SUCCESS;
}

void flash_lock_mapped_storage(int lock)
{
	if (lock)
		mutex_lock(&flash_lock);
	else
		mutex_unlock(&flash_lock);
}

/*****************************************************************************/
/* Host commands */

#if defined(CONFIG_HOSTCMD_FLASH_SPI_INFO) && !defined(BOARD_NPCX_EVB)
/* NPCX EVB uses implementation from spi_flash.c */

static int flash_command_spi_info(struct host_cmd_handler_args *args)
{
	struct ec_response_flash_spi_info *r = args->response;

	flash_get_jedec_id(r->jedec);
	r->reserved0 = 0;
	flash_get_mfr_dev_id(r->mfr_dev_id);
	r->sr1 = flash_get_status1();
	r->sr2 = flash_get_status2();

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_SPI_INFO,
		     flash_command_spi_info,
		     EC_VER_MASK(0));

#endif

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

