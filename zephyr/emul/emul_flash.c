/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_flash.h"
#include "ec_commands.h"
#include "flash.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#define DT_DRV_COMPAT cros_ec_flash_emul

LOG_MODULE_REGISTER(emul_flash);

struct flash_emul_data {
	const struct device *flash_dev;
};

struct flash_emul_cfg {
	/** Pointer to run-time data */
	struct flash_emul_data *data;
};

#define FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

#define DRV_DATA(dev) ((struct flash_emul_data *)(dev)->data)

/* Variables to emulate the protection */
bool ro_protected, all_protected;

static int cros_flash_emul_init(const struct device *dev)
{
	struct flash_emul_data *data = DRV_DATA(dev);

	data->flash_dev = DEVICE_DT_GET(FLASH_DEV);
	if (!device_is_ready(data->flash_dev)) {
		LOG_ERR("device %s not ready", data->flash_dev->name);
		return -ENODEV;
	}

	return EC_SUCCESS;
}

static int flash_check_writable_range(int offset, int size)
{
	/* Check out of range */
	if (offset + size > CONFIG_FLASH_SIZE_BYTES) {
		return EC_ERROR_INVAL;
	}

	/* Check RO protected and within the RO range */
	if (ro_protected &&
	    MAX(CONFIG_WP_STORAGE_OFF, offset) <
		    MIN(CONFIG_WP_STORAGE_OFF + CONFIG_WP_STORAGE_SIZE,
			offset + size)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

static int cros_flash_emul_write(const struct device *dev, int offset, int size,
				 const char *src_data)
{
	int ret = 0;
	struct flash_emul_data *data = DRV_DATA(dev);

	/* Check protection */
	if (all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (flash_check_writable_range(offset, size)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Check invalid data pointer? */
	if (src_data == 0) {
		return -EINVAL;
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	ret = flash_write(data->flash_dev, offset, src_data, size);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

static int cros_flash_emul_erase(const struct device *dev, int offset, int size)
{
	int ret = 0;
	struct flash_emul_data *data = DRV_DATA(dev);

	/* Check protection */
	if (all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (flash_check_writable_range(offset, size)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Address must be aligned to erase size */
	if ((offset % CONFIG_FLASH_ERASE_SIZE) != 0) {
		return -EINVAL;
	}

	/* Erase size must be a non-zero multiple of sectors */
	if ((size == 0) || (size % CONFIG_FLASH_ERASE_SIZE) != 0) {
		return -EINVAL;
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	ret = flash_erase(data->flash_dev, offset, size);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

static int cros_flash_emul_get_protect(const struct device *dev, int bank)
{
	if (all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}
	if (ro_protected && bank >= WP_BANK_OFFSET &&
	    bank < WP_BANK_OFFSET + WP_BANK_COUNT) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

static uint32_t cros_flash_emul_get_protect_flags(const struct device *dev)
{
	uint32_t flags = 0;

	if (ro_protected) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW;
	}
	if (all_protected) {
		flags |= EC_FLASH_PROTECT_ALL_NOW;
	}
	return flags;
}

static int cros_flash_emul_protect_at_boot(const struct device *dev,
					   uint32_t new_flags)
{
	if ((new_flags & (EC_FLASH_PROTECT_RO_AT_BOOT |
			  EC_FLASH_PROTECT_ALL_AT_BOOT)) == 0) {
		/* Clear protection if allowed */
		if (crec_flash_get_protect() & EC_FLASH_PROTECT_GPIO_ASSERTED) {
			return EC_ERROR_ACCESS_DENIED;
		}

		ro_protected = all_protected = false;
		return EC_SUCCESS;
	}

	ro_protected = true;

	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		all_protected = true;
	}

	return EC_SUCCESS;
}

static int cros_flash_emul_protect_now(const struct device *dev, int all)
{
	/* Emulate ALL_NOW only */
	if (all) {
		all_protected = true;
	}

	return EC_SUCCESS;
}

void cros_flash_emul_protect_reset(void)
{
	ro_protected = all_protected = false;
}

void cros_flash_emul_enable_protect(void)
{
	ro_protected = all_protected = true;
}

static int cros_flash_emul_get_jedec_id(const struct device *dev,
					uint8_t *manufacturer, uint16_t *device)
{
	__ASSERT(false, "Not implemented");
	return -EINVAL;
}

static int cros_flash_emul_get_status(const struct device *dev, uint8_t *sr1,
				      uint8_t *sr2)
{
	__ASSERT(false, "Not implemented");
	return -EINVAL;
}

static const struct cros_flash_driver_api emul_cros_flash_driver_api = {
	.init = cros_flash_emul_init,
	.physical_write = cros_flash_emul_write,
	.physical_erase = cros_flash_emul_erase,
	.physical_get_protect = cros_flash_emul_get_protect,
	.physical_get_protect_flags = cros_flash_emul_get_protect_flags,
	.physical_protect_at_boot = cros_flash_emul_protect_at_boot,
	.physical_protect_now = cros_flash_emul_protect_now,
	.physical_get_jedec_id = cros_flash_emul_get_jedec_id,
	.physical_get_status = cros_flash_emul_get_status,
};

static int flash_emul_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

#define FLASH_EMUL(n)                                                         \
	static struct flash_emul_data flash_emul_data_##n = {};               \
                                                                              \
	static const struct flash_emul_cfg flash_emul_cfg_##n = {             \
		.data = &flash_emul_data_##n,                                 \
	};                                                                    \
	DEVICE_DT_INST_DEFINE(n, flash_emul_init, NULL, &flash_emul_data_##n, \
			      &flash_emul_cfg_##n, PRE_KERNEL_1,              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,            \
			      &emul_cros_flash_driver_api)
DT_INST_FOREACH_STATUS_OKAY(FLASH_EMUL);
