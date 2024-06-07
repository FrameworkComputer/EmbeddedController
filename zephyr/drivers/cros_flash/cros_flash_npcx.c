/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT nuvoton_npcx_cros_flash

#include "../drivers/flash/spi_nor.h"
#include "flash.h"
#include "spi_flash_reg.h"
#include "watchdog.h"
#include "write_protect.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/npcx_flash_api_ex.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_flash.h>
#include <soc.h>

LOG_MODULE_REGISTER(cros_flash, LOG_LEVEL_ERR);

static int all_protected; /* Has all-flash protection been requested? */
static int addr_prot_start;
static int addr_prot_length;
static uint8_t saved_sr1;
static uint8_t saved_sr2;

/* Device data */
struct cros_flash_npcx_data {
	const struct device *flash_dev;
};

#define FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

#define DRV_DATA(dev) ((struct cros_flash_npcx_data *)(dev)->data)

#define SPI_NOR_CMD_RDSR2 0x35

/* cros ec flash local functions */
static int cros_flash_npcx_get_status_reg(const struct device *dev,
					  uint8_t cmd_code, uint8_t *reg)
{
	struct npcx_ex_ops_uma_in op_in = {
		.opcode = cmd_code,
		.tx_count = 0,
		.addr_count = 0,
		.rx_count = 1,
	};
	struct npcx_ex_ops_uma_out op_out = {
		.rx_buf = reg,
	};
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	/* Execute UMA transaction */
	return flash_ex_op(data->flash_dev, FLASH_NPCX_EX_OP_EXEC_UMA,
			   (uintptr_t)&op_in, &op_out);
}

static int cros_flash_npcx_wait_ready(const struct device *dev)
{
	int wait_period = 10; /* 10 us period t0 check status register */
	int timeout = (10 * USEC_PER_SEC) / wait_period; /* 10 seconds */

	do {
		uint8_t reg;

		cros_flash_npcx_get_status_reg(dev, SPI_NOR_CMD_RDSR, &reg);
		if ((reg & SPI_NOR_WIP_BIT) == 0)
			break;
		k_usleep(wait_period);
	} while (--timeout); /* Wait for busy bit clear */

	if (timeout) {
		return 0;
	} else {
		return -ETIMEDOUT;
	}
}

/* Check the BUSY bit is cleared and WE bit is set */
static int cros_flash_npcx_wait_ready_and_we(const struct device *dev)
{
	int wait_period = 10; /* 10 us period t0 check status register */
	int timeout = (10 * USEC_PER_SEC) / wait_period; /* 10 seconds */

	do {
		uint8_t reg;

		cros_flash_npcx_get_status_reg(dev, SPI_NOR_CMD_RDSR, &reg);
		if ((reg & SPI_NOR_WIP_BIT) == 0 &&
		    (reg & SPI_NOR_WEL_BIT) != 0)
			break;
		k_usleep(wait_period);
	} while (--timeout); /* Wait for busy bit clear */

	if (timeout) {
		return 0;
	} else {
		return -ETIMEDOUT;
	}
}

static int cros_flash_npcx_set_write_enable(const struct device *dev)
{
	int ret;
	struct npcx_ex_ops_uma_in op_in = {
		.opcode = SPI_NOR_CMD_WREN,
		.tx_count = 0,
		.addr_count = 0,
	};
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	/* Wait for previous operation to complete */
	ret = cros_flash_npcx_wait_ready(dev);
	if (ret != 0) {
		return ret;
	}

	/* Execute write enable command */
	ret = flash_ex_op(data->flash_dev, FLASH_NPCX_EX_OP_EXEC_UMA,
			  (uintptr_t)&op_in, NULL);
	if (ret != 0) {
		return ret;
	}

	/* Wait for flash is not busy */
	return cros_flash_npcx_wait_ready_and_we(dev);
}

static int cros_flash_npcx_set_status_reg(const struct device *dev,
					  uint8_t *reg)
{
	int ret;
	struct npcx_ex_ops_uma_in op_in = {
		.opcode = SPI_NOR_CMD_WRSR,
		.tx_buf = reg,
		.tx_count = 2,
		.addr_count = 0,
	};
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	if (data == 0) {
		return -EINVAL;
	}

	/* Enable write first */
	ret = cros_flash_npcx_set_write_enable(dev);
	if (ret != 0) {
		return ret;
	}

	/* Write status regs */
	ret = flash_ex_op(data->flash_dev, FLASH_NPCX_EX_OP_EXEC_UMA,
			  (uintptr_t)&op_in, NULL);
	if (ret != 0) {
		return ret;
	}

	return cros_flash_npcx_wait_ready(dev);
}

static int cros_flash_npcx_write_protection_set(const struct device *dev,
						bool enable)
{
	struct npcx_ex_ops_qspi_oper_in oper_in = {
		.enable = true,
		.mask = NPCX_EX_OP_INT_FLASH_WP,
	};
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	/* Write protection can be cleared only by core domain reset */
	if (!enable) {
		LOG_ERR("WP can be disabled only via core domain reset ");
		return -ENOTSUP;
	}

	return flash_ex_op(data->flash_dev, FLASH_NPCX_EX_OP_SET_QSPI_OPER,
			   (uintptr_t)&oper_in, NULL);
}

static int cros_flash_npcx_write_protection_is_set(const struct device *dev)
{
	int ret;
	struct npcx_ex_ops_qspi_oper_out oper_out;
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	ret = flash_ex_op(data->flash_dev, FLASH_NPCX_EX_OP_GET_QSPI_OPER,
			  (uintptr_t)NULL, &oper_out);
	if (ret != 0) {
		return ret;
	}

	return (oper_out.oper & NPCX_EX_OP_INT_FLASH_WP) != 0 ? 1 : 0;
}

static int cros_flash_npcx_uma_lock(const struct device *dev, bool enable)
{
	struct npcx_ex_ops_qspi_oper_in oper_in = {
		.mask = NPCX_EX_OP_LOCK_UMA,
	};
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	oper_in.enable = enable;
	return flash_ex_op(data->flash_dev, FLASH_NPCX_EX_OP_SET_QSPI_OPER,
			   (uintptr_t)&oper_in, NULL);
}

static void flash_get_status(const struct device *dev, uint8_t *sr1,
			     uint8_t *sr2)
{
	if (all_protected) {
		*sr1 = saved_sr1;
		*sr2 = saved_sr2;
		return;
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	/* Read status register1 */
	cros_flash_npcx_get_status_reg(dev, SPI_NOR_CMD_RDSR, sr1);
	/* Read status register2 */
	cros_flash_npcx_get_status_reg(dev, SPI_NOR_CMD_RDSR2, sr2);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);
}

/*
 * Check if Status Register Protect 0 (SRP0) bit in the Status 1 Register
 * is set.
 */
static bool flash_check_status_reg_srp(const struct device *dev)
{
	uint8_t sr1, sr2;

	flash_get_status(dev, &sr1, &sr2);

	return (sr1 & SPI_FLASH_SR1_SRP0);
}

static int is_int_flash_protected(const struct device *dev)
{
	return cros_flash_npcx_write_protection_is_set(dev);
}

static int flash_set_status(const struct device *dev, uint8_t sr1, uint8_t sr2)
{
	int rv;
	uint8_t regs[2];

	if (is_int_flash_protected(dev) && flash_check_status_reg_srp(dev)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	regs[0] = sr1;
	regs[1] = sr2;

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);
	rv = cros_flash_npcx_set_status_reg(dev, regs);
	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
}

static void flash_protect_int_flash(const struct device *dev, int enable)
{
	/*
	 * Please notice the type of WP_IF bit is R/W1S. Once it's set,
	 * only rebooting EC can clear it.
	 */
	if (enable)
		cros_flash_npcx_write_protection_set(dev, enable);
}

static void flash_uma_lock(const struct device *dev, int enable)
{
	if (enable && !all_protected) {
		/*
		 * Store SR1 / SR2 for later use since we're about to lock
		 * out all access (including read access) to these regs.
		 */
		flash_get_status(dev, &saved_sr1, &saved_sr2);
	}

	cros_flash_npcx_uma_lock(dev, enable);
	all_protected = enable;
}

static int flash_set_status_for_prot(const struct device *dev, int reg1,
				     int reg2)
{
	int rv;

	/*
	 * Writing SR regs will fail if our UMA lock is enabled. If WP
	 * is deasserted then remove the lock and allow the write.
	 */
	if (all_protected) {
		if (is_int_flash_protected(dev))
			return EC_ERROR_ACCESS_DENIED;

		if (crec_flash_get_protect() & EC_FLASH_PROTECT_GPIO_ASSERTED)
			return EC_ERROR_ACCESS_DENIED;
		flash_uma_lock(dev, 0);
	}

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now before setting them.
	 */
	flash_protect_int_flash(dev, write_protect_is_asserted());

	rv = flash_set_status(dev, reg1, reg2);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	spi_flash_reg_to_protect(reg1, reg2, &addr_prot_start,
				 &addr_prot_length);

	return EC_SUCCESS;
}

static int flash_check_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes)
{
	unsigned int start;
	unsigned int len;
	uint8_t sr1, sr2;
	int rv = EC_SUCCESS;

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now.
	 */
	flash_protect_int_flash(dev, write_protect_is_asserted());

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;

	/* Compute current protect range */
	flash_get_status(dev, &sr1, &sr2);
	rv = spi_flash_reg_to_protect(sr1, sr2, &start, &len);
	if (rv)
		return rv;

	/* Check if ranges overlap */
	if (MAX(start, offset) < MIN(start + len, offset + bytes))
		return EC_ERROR_ACCESS_DENIED;

	return EC_SUCCESS;
}

static int flash_write_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes, int hw_protect)
{
	int rv;
	uint8_t sr1, sr2;

	/* Invalid values */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;

	/* Compute desired protect range */
	flash_get_status(dev, &sr1, &sr2);
	rv = spi_flash_protect_to_reg(offset, bytes, &sr1, &sr2);
	if (rv)
		return rv;

	if (hw_protect)
		sr1 |= SPI_FLASH_SR1_SRP0;

	return flash_set_status_for_prot(dev, sr1, sr2);
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

static void flash_set_quad_enable(const struct device *dev, bool enable)
{
	uint8_t sr1, sr2;

	flash_get_status(dev, &sr1, &sr2);

	/* If QE is the same value, return directly. */
	if (!!(sr2 & SPI_FLASH_SR2_QE) == enable)
		return;

	if (enable)
		sr2 |= SPI_FLASH_SR2_QE;
	else
		sr2 &= ~SPI_FLASH_SR2_QE;
	flash_set_status(dev, sr1, sr2);
}

/* cros ec flash api functions */
static int cros_flash_npcx_init(const struct device *dev)
{
	/* Initialize UMA to unlocked */
	flash_uma_lock(dev, 0);

	/*
	 * Disable flash quad enable to avoid /WP pin function is not
	 * available. */
	flash_set_quad_enable(dev, false);

	/*
	 * Protect status registers of internal spi-flash if WP# is active
	 * during ec initialization.
	 */
	flash_protect_int_flash(dev, write_protect_is_asserted());

	return 0;
}

static int cros_flash_npcx_write(const struct device *dev, int offset, int size,
				 const char *src_data)
{
	int ret = 0;
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* check protection */
	if (flash_check_prot_range(offset, size))
		return EC_ERROR_ACCESS_DENIED;

	/* Invalid data pointer? */
	if (src_data == 0) {
		return -EINVAL;
	}

	/*
	 * If the AP sends a sequence of write commands, we may not have time to
	 * reload the watchdog normally.  Force a reload here to avoid the
	 * watchdog triggering in the middle of flashing.
	 */
	if (IS_ENABLED(CONFIG_WATCHDOG))
		watchdog_reload();

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	ret = flash_write(data->flash_dev, offset, src_data, size);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

BUILD_ASSERT(FLASH_WATCHDOG_RELOAD_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);

static int cros_flash_npcx_erase(const struct device *dev, int offset, int size)
{
	int ret = 0;
	struct cros_flash_npcx_data *data = DRV_DATA(dev);
	size_t reload_size = FLASH_WATCHDOG_RELOAD_SIZE;

	/* check protection */
	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/* check protection */
	if (flash_check_prot_range(offset, size))
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * Offset must be positive, it's alignment check is done in the Zephyr
	 * flash driver
	 */
	if (offset < 0) {
		return -EINVAL;
	}

	/*
	 * Erase size must be positive, its alignment check is done in the
	 * Zephyr flash driver
	 */
	if (size <= 0) {
		return -EINVAL;
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	for (; size > 0; size -= reload_size) {
		/*
		 * Reload the watchdog timer, so that erasing many flash pages
		 * doesn't cause a watchdog reset
		 */
		if (IS_ENABLED(CONFIG_WATCHDOG))
			watchdog_reload();

		/* Start erase */
		ret = flash_erase(data->flash_dev, offset,
				  MIN(reload_size, size));
		if (ret)
			break;

		offset += reload_size;
	}

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

static int cros_flash_npcx_get_protect(const struct device *dev, int bank)
{
	uint32_t addr = bank * CONFIG_FLASH_BANK_SIZE;

	return flash_check_prot_reg(dev, addr, CONFIG_FLASH_BANK_SIZE);
}

static uint32_t cros_flash_npcx_get_protect_flags(const struct device *dev)
{
	uint32_t flags = 0;
	int rv;
	uint8_t sr1, sr2;
	unsigned int start, len;

	/* Check if WP region is protected in status register */
	rv = flash_check_prot_reg(dev, WP_BANK_OFFSET * CONFIG_FLASH_BANK_SIZE,
				  WP_BANK_COUNT * CONFIG_FLASH_BANK_SIZE);
	if (rv == EC_ERROR_ACCESS_DENIED)
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
	else if (rv)
		return EC_FLASH_PROTECT_ERROR_UNKNOWN;

	/*
	 * If the status register protects a range, but SRP0 is not set,
	 * or Quad Enable (QE) is set,
	 * flags should indicate EC_FLASH_PROTECT_ERROR_INCONSISTENT.
	 */
	flash_get_status(dev, &sr1, &sr2);
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

static int cros_flash_npcx_protect_at_boot(const struct device *dev,
					   uint32_t new_flags)
{
	int ret;

	if ((new_flags & (EC_FLASH_PROTECT_RO_AT_BOOT |
			  EC_FLASH_PROTECT_ALL_AT_BOOT)) == 0) {
		/* Clear protection bits in status register */
		return flash_set_status_for_prot(dev, 0, 0);
	}

	ret = flash_write_prot_reg(dev, CONFIG_WP_STORAGE_OFF,
				   CONFIG_WP_STORAGE_SIZE, 1);

	/*
	 * Set UMA_LOCK bit for locking all UMA transaction.
	 * But we still can read directly from flash mapping address
	 */
	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT)
		flash_uma_lock(dev, 1);

	return ret;
}

static int cros_flash_npcx_protect_now(const struct device *dev, int all)
{
	if (all) {
		/*
		 * Set UMA_LOCK bit for locking all UMA transaction.
		 * But we still can read directly from flash mapping address
		 */
		flash_uma_lock(dev, 1);
	} else {
		/* TODO: Implement RO "now" protection */
	}

	return EC_SUCCESS;
}

static int cros_flash_npcx_get_jedec_id(const struct device *dev,
					uint8_t *manufacturer, uint16_t *device)
{
	int ret;
	uint8_t jedec_id[3];
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	ret = flash_read_jedec_id(data->flash_dev, jedec_id);
	if (ret == 0) {
		*manufacturer = jedec_id[0];
		*device = (jedec_id[1] << 8) | jedec_id[2];
	}

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

static int cros_flash_npcx_get_status(const struct device *dev, uint8_t *sr1,
				      uint8_t *sr2)
{
	flash_get_status(dev, sr1, sr2);

	return EC_SUCCESS;
}

/* cros ec flash driver registration */
static const struct cros_flash_driver_api cros_flash_npcx_driver_api = {
	.init = cros_flash_npcx_init,
	.physical_write = cros_flash_npcx_write,
	.physical_erase = cros_flash_npcx_erase,
	.physical_get_protect = cros_flash_npcx_get_protect,
	.physical_get_protect_flags = cros_flash_npcx_get_protect_flags,
	.physical_protect_at_boot = cros_flash_npcx_protect_at_boot,
	.physical_protect_now = cros_flash_npcx_protect_now,
	.physical_get_jedec_id = cros_flash_npcx_get_jedec_id,
	.physical_get_status = cros_flash_npcx_get_status,
};

static int flash_npcx_init(const struct device *dev)
{
	struct cros_flash_npcx_data *data = DRV_DATA(dev);

	data->flash_dev = DEVICE_DT_GET(FLASH_DEV);
	if (!device_is_ready(data->flash_dev)) {
		LOG_ERR("device %s not ready", data->flash_dev->name);
		return -ENODEV;
	}

	return EC_SUCCESS;
}

#if CONFIG_CROS_FLASH_NPCX_INIT_PRIORITY <= CONFIG_FLASH_NPCX_FIU_NOR_INIT
#error "CONFIG_CROS_FLASH_NPCX_INIT_PRIORITY must be greater than" \
	"CONFIG_FLASH_NPCX_FIU_NOR_INIT."
#endif
static struct cros_flash_npcx_data cros_flash_data;
DEVICE_DT_INST_DEFINE(0, flash_npcx_init, NULL, &cros_flash_data, NULL,
		      POST_KERNEL, CONFIG_CROS_FLASH_NPCX_INIT_PRIORITY,
		      &cros_flash_npcx_driver_api);
