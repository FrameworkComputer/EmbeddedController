/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_flash_emul

#include <logging/log.h>
LOG_MODULE_REGISTER(emul_flash);

#include <device.h>
#include <drivers/emul.h>
#include <ec_commands.h>
#include <drivers/cros_flash.h>
#include <sys/__assert.h>

struct flash_emul_data {};

struct flash_emul_cfg {
	/** Label of the device being emulated */
	const char *dev_label;
	/** Pointer to run-time data */
	struct flash_emul_data *data;
};

void system_jump_to_booter(void)
{
}

uint32_t system_get_lfw_address(void)
{
	uint32_t jump_addr = (uint32_t)system_jump_to_booter;
	return jump_addr;
}

enum ec_image system_get_shrspi_image_copy(void)
{
	return EC_IMAGE_RO;
}

void system_set_image_copy(enum ec_image copy)
{
}

static int cros_flash_emul_init(const struct device *dev)
{
	return 0;
}

static int cros_flash_emul_write(const struct device *dev, int offset, int size,
				 const char *src_data)
{
	__ASSERT(false, "Not implemented");
	return -EINVAL;
}

static int cros_flash_emul_erase(const struct device *dev, int offset, int size)
{
	__ASSERT(false, "Not implemented");
	return -EINVAL;
}

static int cros_flash_emul_get_protect(const struct device *dev, int bank)
{
	__ASSERT(false, "Not implemented");
	return -EINVAL;
}

static uint32_t cros_flash_emul_get_protect_flags(const struct device *dev)
{
	return EC_FLASH_PROTECT_ERROR_UNKNOWN;
}

static int cros_flash_emul_protect_at_boot(const struct device *dev,
					   uint32_t new_flags)
{
	__ASSERT(false, "Not implemented");
	return -EINVAL;
}

static int cros_flash_emul_protect_now(const struct device *dev, int all)
{
	__ASSERT(false, "Not implemented");
	return -EINVAL;
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

#define FLASH_EMUL(n)                                                      \
	static struct flash_emul_data flash_emul_data_##n = {              \
	};                                                                 \
									   \
	static const struct flash_emul_cfg flash_emul_cfg_##n = {          \
		.dev_label = DT_INST_LABEL(n),                             \
		.data = &flash_emul_data_##n,                              \
	};                                                                 \
	DEVICE_DT_INST_DEFINE(n, flash_emul_init, NULL,                    \
			      &flash_emul_data_##n, &flash_emul_cfg_##n,   \
			      PRE_KERNEL_1,                                \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,         \
			      &emul_cros_flash_driver_api)
DT_INST_FOREACH_STATUS_OKAY(FLASH_EMUL);
