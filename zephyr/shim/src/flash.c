/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <flash.h>
#include <kernel.h>
#include <logging/log.h>

#include "console.h"
#include "drivers/cros_flash.h"
#include "registers.h"
#include "task.h"
#include "util.h"

LOG_MODULE_REGISTER(shim_flash, LOG_LEVEL_ERR);

#define CROS_FLASH_DEV DT_LABEL(DT_NODELABEL(fiu0))
static const struct device *cros_flash_dev;

K_MUTEX_DEFINE(flash_lock);

/* TODO(b/174873770): Add calls to Zephyr code here */
#ifdef CONFIG_EXTERNAL_STORAGE
void crec_flash_lock_mapped_storage(int lock)
{
	if (lock)
		mutex_lock(&flash_lock);
	else
		mutex_unlock(&flash_lock);
}
#endif

int crec_flash_physical_write(int offset, int size, const char *data)
{
	int rv;

	/* Fail if offset, size, and data aren't at least word-aligned */
	if ((offset | size | (uint32_t)(uintptr_t)data) &
	    (CONFIG_FLASH_WRITE_SIZE - 1))
		return EC_ERROR_INVAL;

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	rv = cros_flash_physical_write(cros_flash_dev, offset, size, data);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
}

int crec_flash_physical_erase(int offset, int size)
{
	int rv;

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	rv = cros_flash_physical_erase(cros_flash_dev, offset, size);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
}

int crec_flash_physical_get_protect(int bank)
{
	return cros_flash_physical_get_protect(cros_flash_dev, bank);
}

uint32_t crec_flash_physical_get_protect_flags(void)
{
	return cros_flash_physical_get_protect_flags(cros_flash_dev);
}

int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	return cros_flash_physical_protect_at_boot(cros_flash_dev, new_flags);
}

int crec_flash_physical_protect_now(int all)
{
	return cros_flash_physical_protect_now(cros_flash_dev, all);
}

int crec_flash_physical_read(int offset, int size, char *data)
{
	int rv;

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);
	rv = cros_flash_physical_read(cros_flash_dev, offset, size, data);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
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

	return 0;
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

/*
 * The priority flash_dev_init should be lower than GPIO initialization because
 * it calls gpio_get_level function.
 */
#if CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY <= \
	CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY
#error "Flash must be initialized after GPIOs"
#endif
SYS_INIT(flash_dev_init, POST_KERNEL, CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY);
