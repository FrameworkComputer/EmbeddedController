/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <flash.h>
#include <kernel.h>
#include <logging/log.h>

#include "console.h"
#include "drivers/cros_flash.h"
#include "gpio.h"
#include "registers.h"
#include "spi_flash_reg.h"
#include "util.h"

LOG_MODULE_REGISTER(shim_flash, LOG_LEVEL_ERR);

#define CROS_FLASH_DEV DT_LABEL(DT_NODELABEL(fiu0))
static const struct device *cros_flash_dev;

static int all_protected; /* Has all-flash protection been requested? */
static int addr_prot_start;
static int addr_prot_length;
static uint8_t flag_prot_inconsistent;
static uint8_t saved_sr1;
static uint8_t saved_sr2;

#define CMD_READ_STATUS_REG              0x05
#define CMD_READ_STATUS_REG2             0x35

static int flash_get_status1(void)
{
	uint8_t reg;

	if (all_protected)
		return saved_sr1;

	cros_flash_get_status_reg(cros_flash_dev, CMD_READ_STATUS_REG, &reg);
	return reg;
}

static int flash_get_status2(void)
{
	uint8_t reg;

	if (all_protected)
		return saved_sr1;

	cros_flash_get_status_reg(cros_flash_dev, CMD_READ_STATUS_REG2, &reg);
	return reg;
}

static int flash_write_status_reg(uint8_t *data)
{
	return cros_flash_set_status_reg(cros_flash_dev, data);
}

static int is_int_flash_protected(void)
{
	return cros_flash_write_protection_is_set(cros_flash_dev);
}

static void flash_protect_int_flash(int enable)
{
	/*
	 * Please notice the type of WP_IF bit is R/W1S. Once it's set,
	 * only rebooting EC can clear it.
	 */
	if (enable)
		cros_flash_write_protection_set(cros_flash_dev, enable);
}

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

	cros_flash_uma_lock(cros_flash_dev, enable);
	all_protected = enable;
}

static int flash_set_status_for_prot(int reg1, int reg2)
{
	uint8_t regs[2];

	/*
	 * Writing SR regs will fail if our UMA lock is enabled. If WP
	 * is deasserted then remove the lock and allow the write.
	 */
	if (all_protected) {
		if (is_int_flash_protected())
			return EC_ERROR_ACCESS_DENIED;

		if (flash_get_protect() & EC_FLASH_PROTECT_GPIO_ASSERTED)
			return EC_ERROR_ACCESS_DENIED;
		flash_uma_lock(0);
	}

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now before setting them.
	 */
#ifdef CONFIG_WP_ACTIVE_HIGH
	flash_protect_int_flash(gpio_get_level(GPIO_WP));
#else
	flash_protect_int_flash(!gpio_get_level(GPIO_WP_L));
#endif /*_CONFIG_WP_ACTIVE_HIGH_*/

	regs[0] = reg1;
	regs[1] = reg2;
	flash_write_status_reg(regs);

	spi_flash_reg_to_protect(reg1, reg2, &addr_prot_start,
				 &addr_prot_length);

	return EC_SUCCESS;
}

static int flash_check_prot_range(unsigned int offset, unsigned int bytes)
{
	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE)
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
#ifdef CONFIG_WP_ACTIVE_HIGH
	flash_protect_int_flash(gpio_get_level(GPIO_WP));
#else
	flash_protect_int_flash(!gpio_get_level(GPIO_WP_L));
#endif /* CONFIG_WP_ACTIVE_HIGH */

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

/* TODO(b/174873770): Add calls to Zephyr code here */

int flash_physical_write(int offset, int size, const char *data)
{
	/* Fail if offset, size, and data aren't at least word-aligned */
	if ((offset | size | (uint32_t)(uintptr_t)data) &
	    (CONFIG_FLASH_WRITE_SIZE - 1))
		return EC_ERROR_INVAL;

	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* check protection */
	if (flash_check_prot_range(offset, size))
		return EC_ERROR_ACCESS_DENIED;

	return cros_flash_physical_write(cros_flash_dev, offset, size, data);
}

int flash_physical_erase(int offset, int size)
{
	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* check protection */
	if (flash_check_prot_range(offset, size))
		return EC_ERROR_ACCESS_DENIED;

	return cros_flash_physical_erase(cros_flash_dev, offset, size);
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
	if (flash_check_prot_reg(WP_BANK_OFFSET * CONFIG_FLASH_BANK_SIZE,
				 WP_BANK_COUNT * CONFIG_FLASH_BANK_SIZE))
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

int flash_physical_protect_at_boot(uint32_t new_flags)
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

int flash_physical_read(int offset, int size, char *data)
{
	return cros_flash_physical_read(cros_flash_dev, offset, size, data);
}

static int flash_dev_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	cros_flash_dev = device_get_binding(CROS_FLASH_DEV);
	if (!cros_flash_dev) {
		LOG_ERR("Fail to find %s", CROS_FLASH_DEV);
		return -ENODEV;
	}
	cros_flash_init(cros_flash_dev);

	/*
	 * Protect status registers of internal spi-flash if WP# is active
	 * during ec initialization.
	 */
#ifdef CONFIG_WP_ACTIVE_HIGH
	flash_protect_int_flash(gpio_get_level(GPIO_WP));
#else
	flash_protect_int_flash(!gpio_get_level(GPIO_WP_L));
#endif /*CONFIG_WP_ACTIVE_HIGH */

	/* Initialize UMA to unlocked */
	flash_uma_lock(0);

	return 0;
}

uint32_t flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
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

/*
 * The priority flash_dev_init should be lower than GPIO initialization because
 * it calls gpio_get_level function.
 */
SYS_INIT(flash_dev_init, PRE_KERNEL_1, 51);
