/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ft_ft9001_cros_flash

#include "flash.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_flash.h>
#include <soc.h>

LOG_MODULE_REGISTER(cros_flash, LOG_LEVEL_ERR);

#if !DT_NODE_EXISTS(DT_CHOSEN(zephyr_flash_controller))
#error "No suitable devicetree overlay specified for zephyr_flash_controller"
#endif

struct cros_flash_ft_config {
	const struct device *flash_dev;
};

#define DRV_CONFIG(dev) ((const struct cros_flash_ft_config *)(dev)->config)

#define FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

static const struct cros_flash_ft_config cros_flash_config = {
	.flash_dev = DEVICE_DT_GET(FLASH_DEV),
};

static int cros_flash_ft_init(const struct device *dev)
{
	return EC_SUCCESS;
}

static int cros_flash_ft_write(const struct device *dev, int offset, int size,
			       const char *src_data)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);

	return flash_write(cfg->flash_dev, offset, src_data, size);
}

static int cros_flash_ft_erase(const struct device *dev, int offset, int size)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);

	return flash_erase(cfg->flash_dev, offset, size);
}

static int cros_flash_ft_get_protect(const struct device *dev, int bank)
{
	return 0;
}

static uint32_t cros_flash_ft_get_protect_flags(const struct device *dev)
{
	return 0;
}

static int cros_flash_ft_protect_at_boot(const struct device *dev,
					 uint32_t new_flags)
{
	return 0;
}

static int cros_flash_ft_protect_now(const struct device *dev, bool all)
{
	return 0;
}

/* cros ec flash driver registration */
static DEVICE_API(cros_flash, cros_flash_spi_nor_driver_api) = {
	.init = cros_flash_ft_init,
	.physical_write = cros_flash_ft_write,
	.physical_erase = cros_flash_ft_erase,
	.physical_get_protect = cros_flash_ft_get_protect,
	.physical_get_protect_flags = cros_flash_ft_get_protect_flags,
	.physical_protect_at_boot = cros_flash_ft_protect_at_boot,
	.physical_protect_now = cros_flash_ft_protect_now,
};

BUILD_ASSERT(CONFIG_FLASH_INIT_PRIORITY <
	     CONFIG_CROS_FLASH_FOCALTECH_INIT_PRIORITY);

static int flash_ft_init(const struct device *dev)
{
	const struct cros_flash_ft_config *cfg = DRV_CONFIG(dev);

	if (!device_is_ready(cfg->flash_dev)) {
		LOG_ERR("device %s not ready", cfg->flash_dev->name);
		return -ENODEV;
	}

	return EC_SUCCESS;
}

DEVICE_DT_INST_DEFINE(0, flash_ft_init, NULL, NULL, &cros_flash_config,
		      POST_KERNEL, CONFIG_CROS_FLASH_FOCALTECH_INIT_PRIORITY,
		      &cros_flash_spi_nor_driver_api);
