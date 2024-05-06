/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it8xxx2_cros_flash

#include "bbram.h"
#include "flash.h"
#include "host_command.h"
#include "system.h"
#include "watchdog.h"

#include <zephyr/drivers/bbram.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_flash.h>
#include <soc.h>

LOG_MODULE_REGISTER(cros_flash, LOG_LEVEL_ERR);

/* Device data */
struct cros_flash_it8xxx2_data {
	bool stuck_locked;
	bool inconsistent_locked;
	bool all_protected;
};

#define GCTRL_IT8XXX2_REG_BASE \
	((struct gctrl_it8xxx2_regs *)DT_REG_ADDR(DT_NODELABEL(gctrl)))

/* Driver convenience defines */
#define DRV_DATA(dev) ((struct cros_flash_it8xxx2_data *)(dev)->data)

static const struct device *const flash_controller =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

#define FWP_REG(bank) (bank / 8)
#define FWP_MASK(bank) (1 << (bank % 8))

enum flash_wp_interface {
	FLASH_WP_HOST = 0x01,
	FLASH_WP_DBGR = 0x02,
	FLASH_WP_EC = 0x04,
};

enum flash_wp_status {
	FLASH_WP_STATUS_PROTECT_RO = EC_FLASH_PROTECT_RO_NOW,
	FLASH_WP_STATUS_PROTECT_ALL = EC_FLASH_PROTECT_ALL_NOW,
};

/**
 * AT_BOOT flags in BBram.
 * We want these regions locked by default, so the flag is reversed:
 * the reset value means locked.
 */
#define IT8XXX2_UNLOCK_RW_AT_BOOT BIT(0)
#define IT8XXX2_UNLOCK_ROLLBACK_AT_BOOT BIT(1)
#define IT8XXX2_UNLOCK_ALL_AT_BOOT BIT(2)

/**
 * Protect flash banks until reboot.
 *
 * @param start_bank    Start bank to protect
 * @param bank_count    Number of banks to protect
 */
static void flash_protect_banks(int start_bank, int bank_count,
				enum flash_wp_interface wp_if)
{
	int bank;

	for (bank = start_bank; bank < start_bank + bank_count; bank++) {
		if (wp_if & FLASH_WP_EC)
			IT83XX_GCTRL_EWPR0PFEC(FWP_REG(bank)) |= FWP_MASK(bank);
		if (wp_if & FLASH_WP_HOST)
			IT83XX_GCTRL_EWPR0PFH(FWP_REG(bank)) |= FWP_MASK(bank);
		if (wp_if & FLASH_WP_DBGR)
			IT83XX_GCTRL_EWPR0PFD(FWP_REG(bank)) |= FWP_MASK(bank);
	}
}

static enum flash_wp_status flash_check_wp(void)
{
	enum flash_wp_status wp_status;
	int all_bank_count, bank;

	all_bank_count = CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE;

	for (bank = 0; bank < all_bank_count; bank++) {
		if (!(IT83XX_GCTRL_EWPR0PFEC(FWP_REG(bank)) & FWP_MASK(bank)))
			break;
	}

	if (bank == WP_BANK_COUNT)
		wp_status = FLASH_WP_STATUS_PROTECT_RO;
	else if (bank == (WP_BANK_COUNT + PSTATE_BANK_COUNT))
		wp_status = FLASH_WP_STATUS_PROTECT_RO;
	else if (bank == all_bank_count)
		wp_status = FLASH_WP_STATUS_PROTECT_ALL;
	else
		wp_status = 0;

	return wp_status;
}

static int read_bbram_flags(uint8_t *data)
{
#ifdef CONFIG_FLASH_PROTECT_NEXT_BOOT
	const struct device *bbram_dev = DEVICE_DT_GET(DT_NODELABEL(bbram));

	return bbram_read(bbram_dev, BBRAM_REGION_OFFSET(unlock_flash_at_boot),
			  BBRAM_REGION_SIZE(unlock_flash_at_boot), data);
#else
	*data = 0;

	return 0;
#endif
}

static int write_bbram_flags(uint8_t data)
{
#ifdef CONFIG_FLASH_PROTECT_NEXT_BOOT
	const struct device *bbram_dev = DEVICE_DT_GET(DT_NODELABEL(bbram));

	return bbram_write(bbram_dev, BBRAM_REGION_OFFSET(unlock_flash_at_boot),
			   BBRAM_REGION_SIZE(unlock_flash_at_boot), &data);
#else
	return 0;
#endif
}

static void try_lock_rw_rb(void)
{
#ifdef CONFIG_FLASH_PROTECT_NEXT_BOOT
	bool need_reset = false;
	uint8_t unlock_flags = 0;
	__maybe_unused bool lock_rw, lock_rb;

	if (read_bbram_flags(&unlock_flags)) {
		LOG_ERR("read_unlock_flags failed, lock all regions.");
		unlock_flags = 0;
	}

#ifdef CONFIG_FLASH_PROTECT_RW
	lock_rw = !(unlock_flags & IT8XXX2_UNLOCK_RW_AT_BOOT);
	if (lock_rw) {
		flash_protect_banks(RW_BANK_OFFSET, RW_BANK_COUNT, FLASH_WP_EC);
	}
	for (int i = 0; i < RW_BANK_COUNT; i++) {
		int bank = RW_BANK_OFFSET + i;

		if ((bool)crec_flash_physical_get_protect(bank) != lock_rw) {
			need_reset = true;
		}
	}
#endif

#ifdef CONFIG_ROLLBACK
	lock_rb = !(unlock_flags & IT8XXX2_UNLOCK_ROLLBACK_AT_BOOT);
	if (lock_rb) {
		flash_protect_banks(ROLLBACK_BANK_OFFSET, ROLLBACK_BANK_COUNT,
				    FLASH_WP_EC);
	}
	for (int i = 0; i < ROLLBACK_BANK_COUNT; i++) {
		int bank = ROLLBACK_BANK_OFFSET + i;

		if ((bool)crec_flash_physical_get_protect(bank) != lock_rb) {
			need_reset = true;
		}
	}
#endif

	if (need_reset) {
		LOG_ERR("Can't modify flash protection, try hard reset!");
		system_reset(SYSTEM_RESET_HARD | SYSTEM_RESET_PRESERVE_FLAGS);
	}
#endif /* CONFIG_FLASH_PROTECT_NEXT_BOOT */
}

/* cros ec flash api functions */
static int cros_flash_it8xxx2_init(const struct device *dev)
{
	struct cros_flash_it8xxx2_data *const data = DRV_DATA(dev);
	int32_t reset_flags, prot_flags, unwanted_prot_flags;

	reset_flags = system_get_reset_flags();
	prot_flags = crec_flash_get_protect();
	unwanted_prot_flags = EC_FLASH_PROTECT_ALL_NOW |
			      EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection.  Nothing additional needs to be done.
	 */
	if (reset_flags & EC_RESET_FLAG_SYSJUMP)
		return EC_SUCCESS;

	if (prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
		/* Protect the entire flash of host interface */
		flash_protect_banks(
			0, CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE,
			FLASH_WP_HOST);
		/* Protect the entire flash of DBGR interface */
		flash_protect_banks(
			0, CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE,
			FLASH_WP_DBGR);

		try_lock_rw_rb();

		/*
		 * Write protect is asserted.  If we want RO flash protected,
		 * protect it now.
		 */
		if ((prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT) &&
		    !(prot_flags & EC_FLASH_PROTECT_RO_NOW)) {
			int rv =
				crec_flash_set_protect(EC_FLASH_PROTECT_RO_NOW,
						       EC_FLASH_PROTECT_RO_NOW);
			if (rv)
				return rv;

			/* Re-read flags */
			prot_flags = crec_flash_get_protect();
		}
	} else {
		/* Don't want RO flash protected */
		unwanted_prot_flags |= EC_FLASH_PROTECT_RO_NOW;
	}

	/* If there are no unwanted flags, done */
	if (!(prot_flags & unwanted_prot_flags))
		return EC_SUCCESS;

	/*
	 * If the last reboot was a power-on reset, it should have cleared
	 * write-protect.  If it didn't, then the flash write protect registers
	 * have been permanently committed and we can't fix that.
	 */
	if (reset_flags & EC_RESET_FLAG_POWER_ON) {
		data->stuck_locked = 1;
		return EC_ERROR_ACCESS_DENIED;
	} else {
		/*
		 * Set inconsistent flag, because there is no software
		 * reset can clear write-protect.
		 */
		data->inconsistent_locked = 1;
		return EC_ERROR_ACCESS_DENIED;
	}

	/* That doesn't return, so if we're still here that's an error */
	return EC_ERROR_UNKNOWN;
}

static int cros_flash_it8xxx2_write(const struct device *dev, int offset,
				    int size, const char *src_data)
{
	struct cros_flash_it8xxx2_data *const data = DRV_DATA(dev);

	if (data->all_protected) {
		return -EACCES;
	}

	/*
	 * If AP sends write flash command continuously, EC might not have
	 * chance to go back to hook task to touch watchdog. Reload watchdog
	 * on each flash write to prevent the reset.
	 */
	if (IS_ENABLED(CONFIG_WATCHDOG))
		watchdog_reload();

	return flash_write(flash_controller, offset, src_data, size);
}

static int cros_flash_it8xxx2_erase(const struct device *dev, int offset,
				    int size)
{
	struct cros_flash_it8xxx2_data *const data = DRV_DATA(dev);
	int ret = 0;

	if (data->all_protected) {
		return -EACCES;
	}
	/*
	 * Before the flash erasing, the interrupts should be disabled. In
	 * the flash erasing loop, the SHI interrupt should be enabled to
	 * handle AP's command, so irq_lock() is not used here.
	 */
	if (IS_ENABLED(CONFIG_ITE_IT8XXX2_INTC)) {
		ite_intc_save_and_disable_interrupts();
	}
	/*
	 * EC still need to handle AP's EC_CMD_GET_COMMS_STATUS command
	 * during erasing.
	 */
	if (IS_ENABLED(HAS_TASK_HOSTCMD) &&
	    IS_ENABLED(CONFIG_HOST_COMMAND_STATUS)) {
		irq_enable(DT_IRQN(DT_NODELABEL(shi0)));
	}
	/* Always use sector erase command */
	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE) {
		ret = flash_erase(flash_controller, offset,
				  CONFIG_FLASH_ERASE_SIZE);
		if (ret)
			break;

		offset += CONFIG_FLASH_ERASE_SIZE;
		/*
		 * If requested erase size is too large at one time on KGD
		 * flash, we need to reload watchdog to prevent the reset.
		 */
		if (IS_ENABLED(CONFIG_WATCHDOG) && (size > 0x10000))
			watchdog_reload();
	}
	/* Restore interrupts */
	if (IS_ENABLED(CONFIG_ITE_IT8XXX2_INTC)) {
		ite_intc_restore_interrupts();
	}

	return ret;
}

static int cros_flash_it8xxx2_get_protect(const struct device *dev, int bank)
{
	ARG_UNUSED(dev);

	return IT83XX_GCTRL_EWPR0PFEC(FWP_REG(bank)) & FWP_MASK(bank);
}

static uint32_t cros_flash_it8xxx2_get_protect_flags(const struct device *dev)
{
	struct cros_flash_it8xxx2_data *const data = DRV_DATA(dev);
	uint32_t flags = 0;
	uint8_t unlock_flags;

	flags |= flash_check_wp();

	if (data->all_protected)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	/* Check if blocks were stuck locked at pre-init */
	if (data->stuck_locked)
		flags |= EC_FLASH_PROTECT_ERROR_STUCK;

	/* Check if flash protection is in inconsistent state at pre-init */
	if (data->inconsistent_locked)
		flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	if (IS_ENABLED(CONFIG_FLASH_PROTECT_NEXT_BOOT) &&
	    !read_bbram_flags(&unlock_flags)) {
		if (!(unlock_flags & IT8XXX2_UNLOCK_RW_AT_BOOT)) {
			flags |= EC_FLASH_PROTECT_RW_AT_BOOT;
		}
		if (!(unlock_flags & IT8XXX2_UNLOCK_ROLLBACK_AT_BOOT)) {
			flags |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
		}
		if (!(unlock_flags & IT8XXX2_UNLOCK_ALL_AT_BOOT)) {
			flags |= EC_FLASH_PROTECT_ALL_AT_BOOT;
		}
	}

	return flags;
}

static int cros_flash_it8xxx2_protect_at_boot(const struct device *dev,
					      uint32_t new_flags)
{
	uint8_t unlock_flags = 0;

	if (!IS_ENABLED(CONFIG_FLASH_PROTECT_NEXT_BOOT)) {
		return -ENOTSUP;
	}

	/* ALL implies RW + RB */
	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		new_flags |= EC_FLASH_PROTECT_RW_AT_BOOT;
		new_flags |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
	}

	if (!(new_flags & EC_FLASH_PROTECT_RW_AT_BOOT)) {
		unlock_flags |= IT8XXX2_UNLOCK_RW_AT_BOOT;
	}
	if (!(new_flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT)) {
		unlock_flags |= IT8XXX2_UNLOCK_ROLLBACK_AT_BOOT;
	}
	if (!(new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT)) {
		unlock_flags |= IT8XXX2_UNLOCK_ALL_AT_BOOT;
	}

	return write_bbram_flags(unlock_flags);
}

static int cros_flash_it8xxx2_protect_now(const struct device *dev, int all)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;
	struct cros_flash_it8xxx2_data *const data = DRV_DATA(dev);

	if (all) {
		/* Protect the entire flash */
		flash_protect_banks(
			0, CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE,
			FLASH_WP_EC);
		data->all_protected = 1;
	} else {
		/* Protect the read-only section and persistent state */
		flash_protect_banks(WP_BANK_OFFSET, WP_BANK_COUNT, FLASH_WP_EC);
#ifdef PSTATE_BANK
		flash_protect_banks(PSTATE_BANK, PSTATE_BANK_COUNT,
				    FLASH_WP_EC);
#endif
	}

	/*
	 * Eflash protect lock register which can only be write 1 and only be
	 * cleared by power-on reset.
	 */
	gctrl_base->GCTRL_EPLR |= IT8XXX2_GCTRL_EPLR_ENABLE;

	return EC_SUCCESS;
}

/* cros ec flash driver registration */
static const struct cros_flash_driver_api cros_flash_it8xxx2_driver_api = {
	.init = cros_flash_it8xxx2_init,
	.physical_write = cros_flash_it8xxx2_write,
	.physical_erase = cros_flash_it8xxx2_erase,
	.physical_get_protect = cros_flash_it8xxx2_get_protect,
	.physical_get_protect_flags = cros_flash_it8xxx2_get_protect_flags,
	.physical_protect_at_boot = cros_flash_it8xxx2_protect_at_boot,
	.physical_protect_now = cros_flash_it8xxx2_protect_now,
};

static int flash_it8xxx2_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (!device_is_ready(flash_controller)) {
		LOG_ERR("device %s not ready", flash_controller->name);
		return -ENODEV;
	}

	return 0;
}

static struct cros_flash_it8xxx2_data cros_flash_data;

DEVICE_DT_INST_DEFINE(0, flash_it8xxx2_init, NULL, &cros_flash_data, NULL,
		      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		      &cros_flash_it8xxx2_driver_api);
