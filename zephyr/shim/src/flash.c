/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <flash.h>
#include <kernel.h>
#include <logging/log.h>
#include <drivers/flash.h>

#include "console.h"
#include "drivers/cros_flash.h"
#include "registers.h"
#include "task.h"
#include "util.h"

LOG_MODULE_REGISTER(shim_flash, LOG_LEVEL_ERR);

#if !DT_HAS_CHOSEN(cros_ec_flash_controller)
#error "cros-ec,flash-controller device must be chosen"
#else
#define cros_flash_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_flash_controller))
#endif
#if !DT_HAS_CHOSEN(zephyr_flash_controller)
#error "zephyr,flash-controller device must be chosen"
#else
#define flash_ctrl_dev DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller))
#endif

K_MUTEX_DEFINE(flash_lock);

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

	/*
	 * We need to call cros_flash driver because the procedure
	 * may differ depending on the chip type e.g. ite chips need to
	 * call watchdog_reload before calling the Zephyr flash driver.
	 */
	rv = cros_flash_physical_write(cros_flash_dev, offset, size, data);

	return rv;
}

int crec_flash_physical_erase(int offset, int size)
{
	int rv;

	/*
	 * We need to call cros_flash driver because the procedure
	 * may differ depending on the chip type e.g. ite chips need to
	 * split a large erase operation and reload watchdog, otherwise
	 * EC reboot happens
	 */
	rv = cros_flash_physical_erase(cros_flash_dev, offset, size);

	return rv;
}

int crec_flash_physical_get_protect(int bank)
{
	/*
	 * We need to call cros_flash driver because Zephyr flash API
	 * doesn't support reading protected areas and the procedure is
	 * different for each flash type.
	 */
	return cros_flash_physical_get_protect(cros_flash_dev, bank);
}

uint32_t crec_flash_physical_get_protect_flags(void)
{
	/*
	 * We need to call cros_flash driver because Zephyr flash API
	 * doesn't support reading protected areas and the procedure is
	 * different for each flash type.
	 */
	return cros_flash_physical_get_protect_flags(cros_flash_dev);
}

int crec_flash_physical_protect_at_boot(uint32_t new_flags)
{
	/*
	 * It is EC specific, so it needs to be implemented in cros_flash driver
	 * per chip.
	 */
	return cros_flash_physical_protect_at_boot(cros_flash_dev, new_flags);
}

int crec_flash_physical_protect_now(int all)
{
	/*
	 * It is EC specific, so it needs to be implemented in cros_flash driver
	 * per chip.
	 */
	return cros_flash_physical_protect_now(cros_flash_dev, all);
}

int crec_flash_physical_read(int offset, int size, char *data)
{
	int rv;

	/*
	 * Lock the physical flash operation here because, we call the Zephyr
	 * driver directly.
	 */
	crec_flash_lock_mapped_storage(1);

	rv = flash_read(flash_ctrl_dev, offset, data, size);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
}

static int flash_dev_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	if (!device_is_ready(cros_flash_dev) ||
	    !device_is_ready(flash_ctrl_dev))
		k_oops();
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

#if IS_ENABLED(CONFIG_SHELL)
static int command_flashchip(const struct shell *shell,
			     size_t argc,
			     char **argv)
{
	uint8_t manufacturer;
	uint16_t device;
	uint8_t status1;
	uint8_t status2;
	int res;

	res = cros_flash_physical_get_status(cros_flash_dev,
					     &status1,
					     &status2);

	if (!res)
		shell_fprintf(shell,
			      SHELL_NORMAL,
			      "Status 1: 0x%02x, Status 2: 0x%02x\n",
			      status1, status2);

	res = cros_flash_physical_get_jedec_id(cros_flash_dev,
					       &manufacturer,
					       &device);

	if (!res)
		shell_fprintf(shell,
			      SHELL_NORMAL,
			      "Manufacturer: 0x%02x, DID: 0x%04x\n",
			      manufacturer, device);

	return 0;
}
SHELL_CMD_REGISTER(flashchip, NULL, "Information about flash chip",
		   command_flashchip);
#endif

/*
 * The priority flash_dev_init should be lower than GPIO initialization because
 * it calls gpio_pin_get_dt function.
 */
#if CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY <= \
	CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY
#error "Flash must be initialized after GPIOs"
#endif
#if CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY <= \
	CONFIG_CROS_FLASH_NPCX_INIT_PRIORITY
#error "CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY must be greater than" \
	"CONFIG_CROS_FLASH_NPCX_INIT_PRIORITY."
#endif
SYS_INIT(flash_dev_init, POST_KERNEL, CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY);
