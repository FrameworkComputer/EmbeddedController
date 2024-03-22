/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "builtin/assert.h"
#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer_chip.h"
#include "registers.h"
#include "spi_flash_reg.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */
#define FLASH_HOOK_VERSION 1

static int all_protected; /* Has all-flash protection been requested? */
static int addr_prot_start;
static int addr_prot_length;

/* SR regs aren't readable when UMA lock is on, so save a copy */
static uint8_t saved_sr1;
static uint8_t saved_sr2;

#ifdef CONFIG_EXTERNAL_STORAGE
#define TRISTATE_FLASH(x)
#else
#define TRISTATE_FLASH(x) flash_tristate(x)
#endif

/* Ensure only one task is accessing flash at a time. */
static struct mutex flash_lock;

/* The previous write protect state before sys jump */
struct flash_wp_state {
	int all_protected;
	uint8_t saved_sr1;
	uint8_t saved_sr2;
};

/*****************************************************************************/
/* flash internal functions */
#if !defined(NPCX_INT_FLASH_SUPPORT)
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
#endif

static void flash_execute_cmd(uint8_t code, uint8_t cts)
{
	/*
	 * Flash mutex must be held while executing UMA commands after
	 * task_start().
	 */
	ASSERT(!task_start_called() || flash_lock.lock);

	/* set UMA_CODE */
	NPCX_UMA_CODE = code;
	/* execute UMA flash transaction */
	NPCX_UMA_CTS = cts;
	while (IS_BIT_SET(NPCX_UMA_CTS, NPCX_UMA_CTS_EXEC_DONE))
		;
}

static void flash_cs_level(int level)
{
	/* Set chip select to high/low level */
	UPDATE_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_SW_CS1, level);
}

static int flash_wait_ready(void)
{
	uint8_t mask = SPI_FLASH_SR1_BUSY;
	const timestamp_t start = get_time();
	const uint32_t timeout_us = 10 * SECOND;
	const timestamp_t deadline = {
		.val = start.val + timeout_us,
	};

	/* Chip Select down. */
	flash_cs_level(0);
	/* Command for Read status register */
	flash_execute_cmd(CMD_READ_STATUS_REG, MASK_CMD_ONLY);
	do {
		/* Read status register */
		NPCX_UMA_CTS = MASK_RD_1BYTE;
		while (IS_BIT_SET(NPCX_UMA_CTS, NPCX_UMA_CTS_EXEC_DONE))
			;
		/* Busy bit is clear */
		if ((NPCX_UMA_DB0 & mask) == 0)
			break;
		usleep(10);
	} while (!timestamp_expired(deadline, NULL)); /* Wait for Busy clear */

	/* Chip Select high. */
	flash_cs_level(1);

	if (timestamp_expired(deadline, NULL))
		return EC_ERROR_TIMEOUT;

	return EC_SUCCESS;
}

static int flash_write_enable(void)
{
	uint8_t mask = SPI_FLASH_SR1_WEL;
	int rv;
	/* Wait for previous operation to complete */
	rv = flash_wait_ready();
	if (rv)
		return rv;

	/* Write enable command */
	flash_execute_cmd(CMD_WRITE_EN, MASK_CMD_ONLY);

	/* Wait for flash is not busy */
	rv = flash_wait_ready();
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

static void flash_get_status(uint8_t *sr1, uint8_t *sr2)
{
	if (all_protected) {
		*sr1 = saved_sr1;
		*sr2 = saved_sr2;
		return;
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Read status register1 */
	flash_execute_cmd(CMD_READ_STATUS_REG, MASK_CMD_RD_1BYTE);
	*sr1 = NPCX_UMA_DB0;
	/* Read status register2 */
	flash_execute_cmd(CMD_READ_STATUS_REG2, MASK_CMD_RD_1BYTE);
	*sr2 = NPCX_UMA_DB0;
	/* Enable tri-state */
	TRISTATE_FLASH(1);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);
}

#ifdef NPCX_INT_FLASH_SUPPORT
static int is_int_flash_protected(void)
{
	return IS_BIT_SET(NPCX_DEV_CTL4, NPCX_DEV_CTL4_WP_IF);
}

static void flash_protect_int_flash(int enable)
{
	/*
	 * Please notice the type of WP_IF bit is R/W1S. Once it's set,
	 * only rebooting EC can clear it.
	 */
	if (enable && !is_int_flash_protected())
		SET_BIT(NPCX_DEV_CTL4, NPCX_DEV_CTL4_WP_IF);
}
#endif

/* Check if Status Register Protect bit 0 is set */
static int flash_check_status_reg_srp(void)
{
	uint8_t sr1, sr2;

	flash_get_status(&sr1, &sr2);

	return !!(sr1 & SPI_FLASH_SR1_SRP0);
}

static int flash_set_status(uint8_t sr1, uint8_t sr2)
{
	if (flash_check_status_reg_srp()) {
#ifdef NPCX_INT_FLASH_SUPPORT
		if (is_int_flash_protected()) {
			return EC_ERROR_ACCESS_DENIED;
		}
#else
		if (crec_flash_get_protect() & EC_FLASH_PROTECT_GPIO_ASSERTED) {
			return EC_ERROR_ACCESS_DENIED;
		}
#endif
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Enable write */
	flash_write_enable();

	NPCX_UMA_DB0 = sr1;
	NPCX_UMA_DB1 = sr2;

	/* Write status register 1/2 */
	flash_execute_cmd(CMD_WRITE_STATUS_REG, MASK_CMD_WR_2BYTE);
	/* Enable tri-state */
	TRISTATE_FLASH(1);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return EC_SUCCESS;
}

static void flash_set_quad_enable(int enable)
{
	uint8_t sr1, sr2;

	flash_get_status(&sr1, &sr2);

	/* If QE is the same value, return directly. */
	if (!!(sr2 & SPI_FLASH_SR2_QE) == enable)
		return;

	if (enable)
		sr2 |= SPI_FLASH_SR2_QE;
	else
		sr2 &= ~SPI_FLASH_SR2_QE;

	flash_set_status(sr1, sr2);
}

#ifdef CONFIG_HOSTCMD_FLASH_SPI_INFO

void flash_get_mfr_dev_id(uint8_t *dest)
{
	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

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

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);
}

#endif /* CONFIG_HOSTCMD_FLASH_SPI_INFO */

void flash_get_jedec_id(uint8_t *dest)
{
	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	/* Disable tri-state */
	TRISTATE_FLASH(0);
	/* Read manufacturer and device ID */
	flash_execute_cmd(CMD_READ_ID, MASK_CMD_RD_3BYTE);
	/* Enable tri-state */
	TRISTATE_FLASH(1);

	dest[0] = NPCX_UMA_DB0;
	dest[1] = NPCX_UMA_DB1;
	dest[2] = NPCX_UMA_DB2;

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);
}

static void flash_uma_lock(int enable)
{
	if (enable && !all_protected) {
		/*
		 * Store SR1 / SR2 for later use since we're about to lock
		 * out all access (including read access) to these regs.
		 */
		flash_get_status(&saved_sr1, &saved_sr2);
	}

	all_protected = enable;
	UPDATE_BIT(NPCX_UMA_ECTS, NPCX_UMA_ECTS_UMA_LOCK, enable);
}

static int flash_set_status_for_prot(int reg1, int reg2)
{
	int rv;

	/*
	 * Writing SR regs will fail if our UMA lock is enabled. If WP
	 * is deasserted then remove the lock and allow the write.
	 */
	if (all_protected) {
#ifdef NPCX_INT_FLASH_SUPPORT
		if (is_int_flash_protected())
			return EC_ERROR_ACCESS_DENIED;
#endif

		if (crec_flash_get_protect() & EC_FLASH_PROTECT_GPIO_ASSERTED)
			return EC_ERROR_ACCESS_DENIED;
		flash_uma_lock(0);
	}

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now before setting them.
	 */
#ifdef NPCX_INT_FLASH_SUPPORT
#ifdef CONFIG_WP_ACTIVE_HIGH
	flash_protect_int_flash(gpio_get_level(GPIO_WP));
#else
	flash_protect_int_flash(!gpio_get_level(GPIO_WP_L));
#endif /*_CONFIG_WP_ACTIVE_HIGH_*/
#endif

	rv = flash_set_status(reg1, reg2);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	spi_flash_reg_to_protect(reg1, reg2, &addr_prot_start,
				 &addr_prot_length);

	return EC_SUCCESS;
}

static int flash_check_prot_range(unsigned int offset, unsigned int bytes)
{
	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;
	/* Check if ranges overlap */
	if (MAX(addr_prot_start, offset) <
	    MIN(addr_prot_start + addr_prot_length, offset + bytes))
		return EC_ERROR_ACCESS_DENIED;

	return EC_SUCCESS;
}

static int flash_check_prot_reg(unsigned int offset, unsigned int bytes)
{
	unsigned int start;
	unsigned int len;
	uint8_t sr1, sr2;
	int rv = EC_SUCCESS;

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now.
	 */
#ifdef NPCX_INT_FLASH_SUPPORT
#ifdef CONFIG_WP_ACTIVE_HIGH
	flash_protect_int_flash(gpio_get_level(GPIO_WP));
#else
	flash_protect_int_flash(!gpio_get_level(GPIO_WP_L));
#endif /* CONFIG_WP_ACTIVE_HIGH */
#endif

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;

	/* Compute current protect range */
	flash_get_status(&sr1, &sr2);
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
	uint8_t sr1, sr2;

	/* Invalid values */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;

	/* Compute desired protect range */
	flash_get_status(&sr1, &sr2);
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
			       const uint8_t *data)
{
	int write_size;
	int rv;

	while (bytes > 0) {
		/* Write length can not go beyond the end of the flash page */
		write_size = MIN(bytes,
				 CONFIG_FLASH_WRITE_IDEAL_SIZE -
					 (offset &
					  (CONFIG_FLASH_WRITE_IDEAL_SIZE - 1)));

		/* Enable write */
		rv = flash_write_enable();
		if (rv)
			return rv;

		/* Burst UMA transaction */
		flash_burst_write(offset, write_size, data);

		/* Wait write completed */
		rv = flash_wait_ready();
		if (rv)
			return rv;

		data += write_size;
		offset += write_size;
		bytes -= write_size;
	}

	return rv;
}

/*****************************************************************************/

int crec_flash_physical_read(int offset, int size, char *data)
{
	int dest_addr = offset;
	uint32_t idx;

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

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
		NPCX_UMA_CTS = MASK_RD_1BYTE;
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
	crec_flash_lock_mapped_storage(0);

	return EC_SUCCESS;
}

int crec_flash_physical_write(int offset, int size, const char *data)
{
	int dest_addr = offset;
	int write_len;
	int rv;

	/* Fail if offset, size, and data aren't at least word-aligned */
	if ((offset | size | (uint32_t)(uintptr_t)data) &
	    (CONFIG_FLASH_WRITE_SIZE - 1))
		return EC_ERROR_INVAL;

	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	/* Disable tri-state */
	TRISTATE_FLASH(0);

	while (size > 0) {
		/* First write multiples of 256, then (size % 256) last */
		write_len = ((size % CONFIG_FLASH_WRITE_IDEAL_SIZE) == size) ?
				    size :
				    CONFIG_FLASH_WRITE_IDEAL_SIZE;

		/* check protection */
		if (flash_check_prot_range(dest_addr, write_len)) {
			rv = EC_ERROR_ACCESS_DENIED;
			break;
		}

		rv = flash_program_bytes(dest_addr, write_len, data);
		if (rv)
			break;

		data += write_len;
		dest_addr += write_len;
		size -= write_len;
	}

	/* Enable tri-state */
	TRISTATE_FLASH(1);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
}

int crec_flash_physical_erase(int offset, int size)
{
	int rv = EC_SUCCESS;
	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

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
		flash_execute_cmd(NPCX_ERASE_COMMAND, MASK_CMD_ADR);

		/* Wait erase completed */
		rv = flash_wait_ready();
		if (rv)
			break;
	}

	/* Enable tri-state */
	TRISTATE_FLASH(1);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
}

int crec_flash_physical_get_protect(int bank)
{
	uint32_t addr = bank * CONFIG_FLASH_BANK_SIZE;

	return flash_check_prot_reg(addr, CONFIG_FLASH_BANK_SIZE);
}

uint32_t crec_flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;
	uint8_t sr1, sr2;
	unsigned int start, len;
	int rv;

	/* Check if WP region is protected in status register */
	rv = flash_check_prot_reg(WP_BANK_OFFSET * CONFIG_FLASH_BANK_SIZE,
				  WP_BANK_COUNT * CONFIG_FLASH_BANK_SIZE);
	if (rv == EC_ERROR_ACCESS_DENIED)
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
	else if (rv)
		return EC_FLASH_PROTECT_ERROR_UNKNOWN;

	/*
	 * If the status register protects a range, but SRP0 is not set, or QE
	 * is set, flags should indicate EC_FLASH_PROTECT_ERROR_INCONSISTENT.
	 */
	flash_get_status(&sr1, &sr2);
	rv = spi_flash_reg_to_protect(sr1, sr2, &start, &len);
	if (rv)
		return EC_FLASH_PROTECT_ERROR_UNKNOWN;
	if (len && (!(sr1 & SPI_FLASH_SR1_SRP0) || (sr2 & SPI_FLASH_SR2_QE)))
		flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	/* Read all-protected state from our shadow copy */
	if (all_protected)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	return flags;
}

int crec_flash_physical_protect_now(int all)
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

int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	int ret;

	if ((new_flags & (EC_FLASH_PROTECT_RO_AT_BOOT |
			  EC_FLASH_PROTECT_ALL_AT_BOOT)) == 0) {
		/* Clear protection bits in status register */
		return flash_set_status_for_prot(0, 0);
	}

	ret = flash_write_prot_reg(CONFIG_WP_STORAGE_OFF,
				   CONFIG_WP_STORAGE_SIZE, 1);

	/*
	 * Set UMA_LOCK bit for locking all UMA transaction.
	 * But we still can read directly from flash mapping address
	 */
	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT)
		flash_uma_lock(1);

	return ret;
}

uint32_t crec_flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t crec_flash_physical_get_writable_flags(uint32_t cur_flags)
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

int crec_flash_physical_restore_state(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	int version, size;
	const struct flash_wp_state *prev;

	/*
	 * If we have already jumped between images, an earlier image
	 * could have applied write protection. Nothing additional needs
	 * to be done.
	 */
	if (reset_flags & EC_RESET_FLAG_SYSJUMP) {
		prev = (const struct flash_wp_state *)system_get_jump_tag(
			FLASH_SYSJUMP_TAG, &version, &size);
		if (prev && version == FLASH_HOOK_VERSION &&
		    size == sizeof(*prev)) {
			all_protected = prev->all_protected;
			saved_sr1 = prev->saved_sr1;
			saved_sr2 = prev->saved_sr2;
		}
		return 1;
	}

	return 0;
}

/*****************************************************************************/
/* High-level APIs */

int crec_flash_pre_init(void)
{
#if !defined(NPCX_INT_FLASH_SUPPORT)
	/* Enable FIU interface */
	flash_pinmux(1);
#endif

#if defined(CONFIG_EXTERNAL_STORAGE) && !defined(NPCX_INT_FLASH_SUPPORT)
	/* Disable tristate all the time */
	CLEAR_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS);
#endif

	/* Initialize UMA to unlocked */
	flash_uma_lock(0);

	/*
	 * Disable flash quad enable to avoid /WP pin function is not
	 * available. */
	flash_set_quad_enable(0);

#ifdef NPCX_INT_FLASH_SUPPORT
	/*
	 * Fix situation when flash protect bit (SRP0) is enabled, but the size
	 * of protected area is 0 or it's not possible to decode protected range
	 * from SR1 and SR2 registers (spi_flash_reg_to_protect() returned
	 * error). This situation can occur if flashing was interrupted
	 * e.g. flashrom was killed while reading from flash:
	 * http://b/328066864#comment12
	 *
	 * Status registers can be modified only when the SRP0 bit and the WP_IF
	 * bit (in DEV_CTL4 register) are not enabled at the same time. The
	 * WP_IF bit is cleared when MCU reboots, it means that once enabled,
	 * the bit can't be cleared by the software.
	 *
	 * The WP_IF bit is set by flash_protect_int_flash() function based on
	 * GPIO_WP status. In our case, the WP_IF bit is clear in RO (because we
	 * are after reboot), but not in RW (because it will be set later in
	 * this function).
	 *
	 * Clearing the status registers before the WP_IF bit is enabled avoids
	 * situation in which we protect status registers with size of protected
	 * area set to 0. We rely on other parts of the system to enable
	 * protection like we rely on them to enable protection when HW WP is
	 * enabled for the first time.
	 */
	if (!is_int_flash_protected()) {
		uint8_t sr1, sr2;
		unsigned int prot_start, prot_length;
		int rv;

		flash_get_status(&sr1, &sr2);
		rv = spi_flash_reg_to_protect(sr1, sr2, &prot_start,
					      &prot_length);

		if (rv || ((sr1 & SPI_FLASH_SR1_SRP0) && prot_length == 0)) {
			rv = flash_set_status(0, 0);
			if (rv) {
				CPRINTS("Failed to clear invalid status: %d",
					rv);
			}
		}
	}
#endif

	/*
	 * Protect status registers of internal spi-flash if WP# is active
	 * during ec initialization.
	 */
#ifdef NPCX_INT_FLASH_SUPPORT
#ifdef CONFIG_WP_ACTIVE_HIGH
	flash_protect_int_flash(gpio_get_level(GPIO_WP));
#else
	flash_protect_int_flash(!gpio_get_level(GPIO_WP_L));
#endif /*CONFIG_WP_ACTIVE_HIGH */
#endif
	crec_flash_physical_restore_state();

	return EC_SUCCESS;
}

void crec_flash_lock_mapped_storage(int lock)
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

static enum ec_status flash_command_spi_info(struct host_cmd_handler_args *args)
{
	struct ec_response_flash_spi_info *r = args->response;

	flash_get_jedec_id(r->jedec);
	r->reserved0 = 0;
	flash_get_mfr_dev_id(r->mfr_dev_id);
	flash_get_status(&r->sr1, &r->sr2);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_SPI_INFO, flash_command_spi_info,
		     EC_VER_MASK(0));

#endif

#ifdef CONFIG_CMD_FLASH_TRISTATE
#ifdef NPCX_INT_FLASH_SUPPORT
#error "Flash tristate is not relevant when internal flash is used."
#endif
static void flash_tristate(int enable)
{
	/* Enable/Disable FIU pins to tri-state */
	UPDATE_BIT(NPCX_DEVCNT, NPCX_DEVCNT_F_SPI_TRIS, enable);
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
/* Console commands */

static int command_flash_spi_sel_lock(int argc, const char **argv)
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
			"[on | off]", "Lock spi flash interface selection");

static int command_flash_tristate(int argc, const char **argv)
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
DECLARE_CONSOLE_COMMAND(flash_tristate, command_flash_tristate, "[on | off]",
			"Tristate spi flash pins");
#endif /* CONFIG_CMD_FLASH_TRISTATE */

static int command_flash_chip(int argc, const char **argv)
{
	uint8_t jedec_id[3];
	uint8_t sr1, sr2;

	flash_get_status(&sr1, &sr2);
	ccprintf("Status 1: 0x%02x, Status 2: 0x%02x\n", sr1, sr2);

	flash_get_jedec_id(jedec_id);
	ccprintf("Manufacturer: 0x%02x, DID: 0x%02x%02x\n", jedec_id[0],
		 jedec_id[1], jedec_id[2]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashchip, command_flash_chip, NULL,
			"Print flash chip info");

static void flash_preserve_state(void)
{
	struct flash_wp_state state;

	state.all_protected = all_protected;
	state.saved_sr1 = saved_sr1;
	state.saved_sr2 = saved_sr2;

	system_add_jump_tag(FLASH_SYSJUMP_TAG, FLASH_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, flash_preserve_state, HOOK_PRIO_DEFAULT);

#ifdef NPCX_INT_FLASH_SUPPORT
static int flash_write_disable(void)
{
	uint8_t mask = SPI_FLASH_SR1_WEL;
	int rv;
	/* Wait for previous operation to complete */
	rv = flash_wait_ready();
	if (rv)
		return rv;

	/* Write enable command */
	flash_execute_cmd(CMD_WRITE_DIS, MASK_CMD_ONLY);

	/* Wait for flash is not busy */
	rv = flash_wait_ready();
	if (rv)
		return rv;

	if (NPCX_UMA_DB0 & mask)
		return EC_SUCCESS;
	else
		return EC_ERROR_BUSY;
}

bool flash_control_register_locked(void)
{
	/* The name Flash Control Register lock is based on the stm32
	 * implementation. The closest analogy is to use the Status Register
	 * Write Enable Latch (WEL) bit.
	 *
	 * Per section 4.27.4 of the datasheet writing is locked until
	 * SPI_FLASH_SR1_WEL is set to 1
	 */
	return is_int_flash_protected() ||
	       ((NPCX_UMA_DB0 & SPI_FLASH_SR1_WEL) == 0);
}

void unlock_flash_control_register(void)
{
	/* The name Flash Control Register Lock is based on the stm32
	 * implementation. The closest analogy is to call flash_write_enable
	 */
	crec_flash_lock_mapped_storage(1);
	flash_write_enable();
	crec_flash_lock_mapped_storage(0);
}

void lock_flash_control_register(void)
{
	/* The name Flash Control Register lock is based on the stm32
	 * implementation. The closest analogy is to call flash_write_disable
	 */
	crec_flash_lock_mapped_storage(1);
	flash_write_disable();
	crec_flash_lock_mapped_storage(0);
}

void disable_flash_control_register(void)
{
	flash_protect_int_flash(1);
}
#endif /* NPCX_INT_FLASH_SUPPORT */
