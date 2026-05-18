/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Design description.
 *
 * Possible option for flash protection highly depends on hardware.
 * This driver is implemented for Focaltech ft9001, using XIP mode. Such chips
 * use SPI for fetching instructions from flash. There is no flash controller
 * that introduces additional protection features. From hardware perspective,
 * the only available flash protection mechanism is the one provided by a
 * connected flash chip.
 *
 * Additionally, such chips require QSPI mode to speed up execution (faster
 * instructions fetching). That means, the hardware #WP pin can not be used,
 * because it is utilized for data transfer. Software locking of the status and
 * configuration registers has been added in flash driver to somehow emulate
 * behaviour of the #WP pin.
 *
 * The chip hardware flash protection itself is used to identify _AT_BOOT flags.
 * The flash protection, combined with the register lock mechanism identifies
 * _NOW flags.
 *
 * It means, _NOW flags are not preserved during sysjump, because the register
 * lock is cleared. However, the register lock mechanism tries to "mimic" the
 * #WP pin protection so it is always enabled at the beginning of the boot if
 * GPIO_WP is asserted.
 *
 * The driver allows extending flash protection range, even if the registers
 * lock is enabled. Additionally, the driver always allows modifying the status
 * registers (it disables the register lock temporarily) if the GPIO_WP is not
 * asserted.
 */

#define DT_DRV_COMPAT ft_ft9001_cros_flash

#include "../drivers/flash/spi_nor.h"
#include "flash.h"
#include "spi_flash_reg.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/ft90_flash_api_ex.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/minmax.h>

#include <drivers/cros_flash.h>
#include <soc.h>

LOG_MODULE_REGISTER(cros_flash, LOG_LEVEL_ERR);

/* The flash protection for spi nor chips goes from bottom to top or from
 * top to bottom (for WPS=0). The EC application uses the from bottom to top
 * scheme for [Bootloader ->] -> RO -> [RB ->] -> RW protection.
 */
#define FLASH_PROTECTION_START 0

struct cros_flash_ft_xip_data {
	const struct device *flash_dev;
	struct k_mutex flash_lock;
};

#define SPI_FLASH_CR_WPS BIT(2)

#define FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

#define DRV_DATA(dev) ((struct cros_flash_ft_xip_data *)(dev)->data)

static int flash_ft_xip_get_status_regs(const struct device *dev,
					struct ft_xip_ex_ops_get_out *op_out)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);

	return flash_ex_op(data->flash_dev, FLASH_FT_XIP_EX_OP_GET_STATUS_REGS,
			   (uintptr_t)NULL, op_out);
}

static int flash_ft_xip_set_status_regs(const struct device *dev,
					struct ft_xip_ex_ops_set_in *op_in)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);

	return flash_ex_op(data->flash_dev, FLASH_FT_XIP_EX_OP_SET_STATUS_REGS,
			   (uintptr_t)op_in, 0);
}

static int flash_ft_xip_lock_status(const struct device *dev, bool enable)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);
	struct ft_xip_ex_ops_lock_in op_in = {
		.enable = enable,
	};

	return flash_ex_op(data->flash_dev, FLASH_FT_XIP_EX_OP_LOCK,
			   (uintptr_t)&op_in, 0);
}

static int flash_ft_xip_get_lock_status(const struct device *dev, bool *state)
{
	int ret;
	struct ft_xip_ex_ops_lock_state_out op_out;
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);

	ret = flash_ex_op(data->flash_dev, FLASH_FT_XIP_EX_OP_LOCK_STATE,
			  (uintptr_t)NULL, &op_out);
	if (!ret) {
		*state = op_out.state;
	}

	return ret;
}

static int cros_flash_ft_xip_get_status(const struct device *dev, uint8_t *sr1,
					uint8_t *sr2)
{
	struct ft_xip_ex_ops_get_out op_out;
	int ret;

	ret = flash_ft_xip_get_status_regs(dev, &op_out);
	if (!ret) {
		*sr1 = op_out.regs[0];
		*sr2 = op_out.regs[1];
	}

	return ret;
}

static int get_prot_reg(const struct device *dev, uint32_t *start,
			uint32_t *end)
{
	unsigned int len;
	uint8_t sr1, sr2;
	int ret;

	/* Compute current protect range */
	ret = cros_flash_ft_xip_get_status(dev, &sr1, &sr2);
	if (ret) {
		return ret;
	}

	ret = spi_flash_reg_to_protect(sr1, sr2, start, &len);
	if (ret) {
		return ret;
	}
	*end = *start + len;

	return EC_SUCCESS;
}

static int check_prot_reg(const struct device *dev, unsigned int offset,
			  unsigned int bytes)
{
	uint32_t prot_start, prot_end;
	int ret;

	/* Validate input params */
	if ((bytes > CONFIG_FLASH_SIZE_BYTES) ||
	    ((CONFIG_FLASH_SIZE_BYTES - bytes) < offset)) {
		return EC_ERROR_INVAL;
	}

	ret = get_prot_reg(dev, &prot_start, &prot_end);
	if (ret) {
		return EC_ERROR_UNKNOWN;
	}

	/* Check if ranges overlap, even partially. */
	if (max(prot_start, offset) < min(prot_end, offset + bytes)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

static int set_status_for_prot(const struct device *dev, uint8_t reg1,
			       uint8_t reg2)
{
	int ret;
	struct ft_xip_ex_ops_set_in op_in;

	/* BP0-4 bits. */
	op_in.masks[0] = 0x7c;
	/* CMP bit. */
	op_in.masks[1] = 0x40;
	op_in.masks[2] = 0;

	op_in.regs[0] = reg1;
	op_in.regs[1] = reg2;

	/* Update only protection related bits */
	ret = flash_ft_xip_set_status_regs(dev, &op_in);

	return ret;
}

static int set_flash_prot(const struct device *dev, uint32_t offset,
			  uint32_t bytes)
{
	int rv;
	uint8_t sr1, sr2;

	/* Validate input params */
	if ((bytes > CONFIG_FLASH_SIZE_BYTES) ||
	    ((CONFIG_FLASH_SIZE_BYTES - bytes) < offset)) {
		return EC_ERROR_INVAL;
	}

	/* Compute desired protect range */
	rv = spi_flash_protect_to_reg(offset, bytes, &sr1, &sr2);
	if (rv) {
		return rv;
	}

	return set_status_for_prot(dev, sr1, sr2);
}

static uint32_t cros_flash_ft_xip_get_protect_flags(const struct device *dev)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);
	uint32_t flags = 0;
	struct ft_xip_ex_ops_get_out op_out;
	unsigned int prot_start;
	unsigned int prot_end;
	unsigned int prot_len;
	int ret;
	bool status_locked;

	k_mutex_lock(&data->flash_lock, K_FOREVER);

	ret = flash_ft_xip_get_lock_status(dev, &status_locked);
	if (ret) {
		flags = EC_FLASH_PROTECT_ERROR_UNKNOWN;
		goto unlock_flash;
	}

	ret = flash_ft_xip_get_status_regs(dev, &op_out);
	if (ret) {
		flags = EC_FLASH_PROTECT_ERROR_UNKNOWN;
		goto unlock_flash;
	}

	/*
	 * Make sure per block protection is disabled (WPS) and only software
	 * write protection is enabled. QSPI mode is required to speed up the
	 * XIP mode. That means the WP pin is not used for protection.
	 */
	if ((op_out.regs[0] & SPI_FLASH_SR1_SRP0) ||
	    (op_out.regs[1] & SPI_FLASH_SR2_SRP1) ||
	    (op_out.regs[2] & SPI_FLASH_CR_WPS)) {
		flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		goto unlock_flash;
	}

	ret = spi_flash_reg_to_protect(op_out.regs[0], op_out.regs[1],
				       &prot_start, &prot_len);
	if (ret) {
		flags = EC_FLASH_PROTECT_ERROR_UNKNOWN;
		goto unlock_flash;
	}

	/* Check unexpected state. */
	if (prot_start != FLASH_PROTECTION_START) {
		/* Treat this as inconsistent error, because it can be fixed by
		 * re-applying protection flags */
		flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		goto unlock_flash;
	}
	prot_end = prot_start + prot_len;

	/* Check if ranges fully overlap. This logic assumes a certain flash
	 * layout: RO -> ROLLBACKS -> RW. */
	if (prot_end >= (CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE)) {
		flags |= EC_FLASH_PROTECT_ALL_AT_BOOT |
			 EC_FLASH_PROTECT_RO_AT_BOOT;
#ifdef CONFIG_ROLLBACK
		flags |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
	} else if (prot_end >= (CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE)) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT |
			 EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif /* CONFIG_ROLLBACK */
	} else if (prot_end >=
		   (CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE)) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
	}

	/* Status registers are locked. Add proper _NOW flags. */
	if (status_locked) {
		if (flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
			flags |= EC_FLASH_PROTECT_ALL_NOW |
				 EC_FLASH_PROTECT_RO_NOW;
#ifdef CONFIG_ROLLBACK
			flags |= EC_FLASH_PROTECT_ROLLBACK_NOW;
		} else if (flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
			flags |= EC_FLASH_PROTECT_ROLLBACK_NOW |
				 EC_FLASH_PROTECT_RO_NOW;
#endif /* CONFIG_ROLLBACK */
		} else if (flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
			flags |= EC_FLASH_PROTECT_RO_NOW;
		}
	}

unlock_flash:
	k_mutex_unlock(&data->flash_lock);
	return flags;
}

static int cros_flash_ft_xip_get_protect(const struct device *dev, int bank)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);
	uint32_t addr_start = bank * CONFIG_FLASH_BANK_SIZE;
	int ret;
	bool status_locked;

	k_mutex_lock(&data->flash_lock, K_FOREVER);
	ret = flash_ft_xip_get_lock_status(dev, &status_locked);
	if (ret) {
		goto unlock_flash;
	}

	/*
	 * The physical_get_protect returns protection state until reboot,
	 * so it is used to determine *_NOW flags.
	 *
	 * Make sure flash protection is enabled and status registers are
	 * locked.
	 */
	if (status_locked || (addr_start < CONFIG_WP_STORAGE_OFF)) {
		/*
		 * The check_prot_reg() function reports that the region is
		 * protected, even if only part of the region protected, but in
		 * this case:
		 * - The size of the checked region is always
		 * CONFIG_FLASH_BANK_SIZE
		 * - The region address is also aligned to
		 * CONFIG_FLASH_BANK_SIZE
		 * - CONFIG_FLASH_BANK_SIZE is a flash protection unit
		 * This means that the checked region can't be protected
		 * partially, so we are safe here.
		 */
		ret = check_prot_reg(dev, addr_start, CONFIG_FLASH_BANK_SIZE);
		/*
		 * In case of other return codes, including all types of errors,
		 * return not protected status for safety reasons.
		 */
		if (ret != EC_ERROR_ACCESS_DENIED) {
			ret = 0;
		}
	} else {
		ret = 0;
	}
unlock_flash:
	k_mutex_unlock(&data->flash_lock);
	return ret;
}

static int cros_flash_ft_xip_protect_at_boot(const struct device *dev,
					     uint32_t new_flags)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);
	int ret;
	bool status_locked;
	uint32_t new_prot_end;
	uint32_t curr_prot_start;
	uint32_t curr_prot_end;

	k_mutex_lock(&data->flash_lock, K_FOREVER);

	ret = flash_ft_xip_get_lock_status(dev, &status_locked);
	if (ret) {
		goto unlock_flash;
	}

	/* There is no independent protection of each section. Protection of WP
	 * is within protection ranges of Rollbacks */
	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		new_prot_end = CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE;
#ifdef CONFIG_ROLLBACK
	} else if (new_flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
		new_prot_end = CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE;
#endif /* CONFIG_ROLLBACK */
	} else if (new_flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
		new_prot_end = CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE;
	} else {
		/* Disable protection of WP section, but do not disable
		 * protection of the flash header. */
		new_prot_end = CONFIG_WP_STORAGE_OFF;
	}

	unsigned int key = irq_lock();

	ret = get_prot_reg(dev, &curr_prot_start, &curr_prot_end);
	if (ret) {
		goto unlock_irq;
	}

	/* Just exit if the protection range matches new values. */
	if ((curr_prot_start == FLASH_PROTECTION_START) &&
	    (curr_prot_end == new_prot_end)) {
		goto unlock_irq;
	}

	if (status_locked) {
		/* Allow modifying status registers in case deasserted WP, but
		 * restore the lock in case WP is asserted again. */
		if (write_protect_is_asserted()) {
			/* In case of locked status registers and WP asserted,
			 * allow changing protection range only if it is
			 * extending the range. */
			if (new_prot_end < curr_prot_end) {
				ret = EC_ERROR_ACCESS_DENIED;
				goto unlock_irq;
			}
		}
		ret = flash_ft_xip_lock_status(dev, false);
	}

	if (!ret) {
		ret = set_flash_prot(dev, FLASH_PROTECTION_START, new_prot_end);
	}

	/* Always lock the status register if it was locked before. */
	if (status_locked) {
		int ret2;
		ret2 = flash_ft_xip_lock_status(dev, true);
		if (ret2) {
			LOG_ERR("Failed to restore status register lock");
			/* Panic due to security related issue. Flash protection
			 * state is unknown. */
			k_panic();
		}
	}

unlock_irq:
	irq_unlock(key);
unlock_flash:
	k_mutex_unlock(&data->flash_lock);
	return ret;
}

static int cros_flash_ft_xip_protect_now(const struct device *dev, bool all)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);
	int ret;

	k_mutex_lock(&data->flash_lock, K_FOREVER);
	/* Set flash protection and lock the status. */
	if (all) {
		ret = cros_flash_ft_xip_protect_at_boot(
			dev, EC_FLASH_PROTECT_ALL_AT_BOOT);
	} else {
		ret = cros_flash_ft_xip_protect_at_boot(
			dev, EC_FLASH_PROTECT_RO_AT_BOOT);
	}
	if (!ret) {
		ret = flash_ft_xip_lock_status(dev, true);
	}
	k_mutex_unlock(&data->flash_lock);

	return ret;
}

static int check_operation_status(const struct device *dev)
{
	int ret = 0;

#ifdef CONFIG_SPI_FLASH_P25Q16
	uint8_t sr1 = 0, sr2 = 0;
	const uint8_t ep_fail_mask = 0x04;

	ret = cros_flash_ft_xip_get_status(dev, &sr1, &sr2);
	/* Check the EP_FAIL bit identifying Erase/Program Fail. */
	if (!ret && (sr2 & ep_fail_mask)) {
		ret = EC_ERROR_UNKNOWN;
	}
#endif
	return ret;
}

static int cros_flash_ft_xip_write(const struct device *dev, int offset,
				   int size, const char *src_data)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);
	int ret = 0;

	if ((offset < 0) || (size < 0)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->flash_lock, K_FOREVER);
	/* Check protection */
	ret = check_prot_reg(dev, offset, size);
	if (ret) {
		goto unlock_flash;
	}

	ret = flash_write(data->flash_dev, offset, src_data, size);
	if (ret) {
		goto unlock_flash;
	}

	ret = check_operation_status(dev);

unlock_flash:
	k_mutex_unlock(&data->flash_lock);
	return ret;
}

static int cros_flash_ft_xip_erase(const struct device *dev, int offset,
				   int size)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);
	int ret = 0;

	if ((offset < 0) || (size < 0)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->flash_lock, K_FOREVER);
	/* Check protection */
	ret = check_prot_reg(dev, offset, size);
	if (ret) {
		goto unlock_flash;
	}

	ret = flash_erase(data->flash_dev, offset, size);
	if (ret) {
		goto unlock_flash;
	}

	ret = check_operation_status(dev);

unlock_flash:
	k_mutex_unlock(&data->flash_lock);
	return ret;
}

static int cros_flash_ft_xip_init(const struct device *dev)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);
	struct ft_xip_ex_ops_set_in op_in = { .regs = { 0 } };
	int ret;

	/* Make sure to clear SRP bits - only software protection. */
	op_in.masks[0] = SPI_FLASH_SR1_SRP0;
	op_in.masks[1] = SPI_FLASH_SR2_SRP1;
	/* Make sure to clear WPS bit - per block protection. */
	op_in.masks[2] = SPI_FLASH_CR_WPS;

	k_mutex_lock(&data->flash_lock, K_FOREVER);
	ret = flash_ft_xip_set_status_regs(dev, &op_in);
	if (ret) {
		goto unlock_flash;
	}

	if (!system_is_in_rw()) {
		uint32_t flags = cros_flash_ft_xip_get_protect_flags(dev);
		/*
		 * The RW protection has to be disabled in RO to have
		 * possibility of RW update. Changing the status registers won't
		 * be possible after locking it, so disable the RW protection at
		 * the beginning of the RO boot to allow RW update. RWSIG can
		 * re-enable that before the jump.
		 *
		 * Changing the status registers takes ~8ms, so it increases
		 * boot time a bit, if the change is really needed.
		 *
		 * There is an additional concern about flash wear-out in case
		 * of RWSIG. The status registers have the same number of
		 * erase/write cycles as other flash pages, which is e.g. 100k
		 * for Puya P25Q16, so it shouldn't be an issue.
		 * There is a possibility of volatile status register write, but
		 * it is not supported by all flash NOR chips.
		 * Additionally, the cros_flash driver wouldn't know when the
		 * volatile status register write can be used or not.
		 */
		if (flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
			ret = cros_flash_ft_xip_protect_at_boot(
				dev, EC_FLASH_PROTECT_RO_AT_BOOT);
			if (ret) {
				goto unlock_flash;
			}
		}
	}

	if (write_protect_is_asserted()) {
		/* Emulate behaviour of #WP pin and the lock status registers.
		 */
		ret = flash_ft_xip_lock_status(dev, true);
	}

unlock_flash:
	k_mutex_unlock(&data->flash_lock);
	return ret;
}

static DEVICE_API(cros_flash, cros_flash_ft_xip_driver_api) = {
	.init = cros_flash_ft_xip_init,
	.physical_write = cros_flash_ft_xip_write,
	.physical_erase = cros_flash_ft_xip_erase,
	.physical_get_protect = cros_flash_ft_xip_get_protect,
	.physical_get_protect_flags = cros_flash_ft_xip_get_protect_flags,
	.physical_protect_at_boot = cros_flash_ft_xip_protect_at_boot,
	.physical_protect_now = cros_flash_ft_xip_protect_now,
	.physical_get_status = cros_flash_ft_xip_get_status,
};

static int flash_ft_xip_init(const struct device *dev)
{
	struct cros_flash_ft_xip_data *data = DRV_DATA(dev);

	data->flash_dev = DEVICE_DT_GET(FLASH_DEV);
	if (!device_is_ready(data->flash_dev)) {
		LOG_ERR_DEVICE_NOT_READY(data->flash_dev);
		return -ENODEV;
	}
	k_mutex_init(&data->flash_lock);

	return EC_SUCCESS;
}

static struct cros_flash_ft_xip_data cros_flash_data;
DEVICE_DT_INST_DEFINE(0, flash_ft_xip_init, NULL, &cros_flash_data, NULL,
		      POST_KERNEL, CONFIG_CROS_FLASH_FOCALTECH_INIT_PRIORITY,
		      &cros_flash_ft_xip_driver_api);
