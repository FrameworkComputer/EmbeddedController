/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "drivers/cros_flash.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <flash.h>

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

test_mockable int crec_flash_physical_write(int offset, int size,
					    const char *data)
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

test_mockable int crec_flash_physical_read(int offset, int size, char *data)
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

#if defined(CONFIG_FLASH_EX_OP_ENABLED)
void crec_flash_reset(void)
{
	/*
	 * Lock the physical flash operation here because, we call the Zephyr
	 * driver directly.
	 */
	crec_flash_lock_mapped_storage(1);

	flash_ex_op(flash_ctrl_dev, FLASH_EX_OP_RESET, (const uintptr_t)NULL,
		    NULL);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);
}
#endif

static int flash_dev_init(void)
{
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

#if IS_ENABLED(CONFIG_PLATFORM_EC_USE_ZEPHYR_FLASH_PAGE_LAYOUT)
int crec_flash_bank_size(int bank)
{
	int rv;
	struct flash_pages_info info;

	rv = flash_get_page_info_by_idx(flash_ctrl_dev, bank, &info);

	if (rv)
		return -1;

	return info.size;
}

int crec_flash_bank_erase_size(int bank)
{
	return crec_flash_bank_size(bank);
}

int crec_flash_bank_index(int offset)
{
	int rv;
	struct flash_pages_info info;

	rv = flash_get_page_info_by_offs(flash_ctrl_dev, offset, &info);

	if (rv)
		return -1;

	return info.index;
}

int crec_flash_bank_count(int offset, int size)
{
	int begin, end;

	if (size < 1)
		return -1;

	begin = crec_flash_bank_index(offset);
	end = crec_flash_bank_index(offset + size - 1);

	if (begin < 0 || end < 0)
		return -1;

	return end - begin + 1;
}

int crec_flash_bank_start_offset(int bank)
{
	int rv;
	struct flash_pages_info info;

	rv = flash_get_page_info_by_idx(flash_ctrl_dev, bank, &info);

	if (rv)
		return -1;

	return info.start_offset;
}

/*
 * Flash_get_region() is used to get information about region which contains
 * 'start_idx' sector. Information about region is saved in a ec_flash_bank
 * structure. Function returns EC_RES_IN_PROGRESS if there are more regions.
 * EC_RES_SUCCESS is returned if the region is the last one. EC_RES_ERROR is
 * returned if there was an error.
 *
 * Please note that 'start_idx' should point to first sector of the region
 * otherwise number of sectors in region will be wrong.
 */
static int flash_get_region(size_t start_idx, struct ec_flash_bank *region)
{
	struct flash_pages_info first, next;
	size_t total_pages;
	int rv;

	total_pages = flash_get_page_count(flash_ctrl_dev);
	rv = flash_get_page_info_by_idx(flash_ctrl_dev, start_idx, &first);
	if (rv)
		return EC_RES_ERROR;

	/* Region has sectors with the same size, save these information now. */
	region->count = 1;
	region->size_exp = __fls(first.size);
	region->write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE);
	region->erase_size_exp = __fls(first.size);
	region->protect_size_exp = __fls(first.size);

	for (size_t i = start_idx + 1; i < total_pages; i++) {
		rv = flash_get_page_info_by_idx(flash_ctrl_dev, i, &next);
		if (rv)
			return EC_RES_ERROR;

		/*
		 * If size of the next page is different than size of the first
		 * page of this region then we know how many pages the region
		 * has and this is not the last region.
		 */
		if (next.size != first.size)
			return EC_RES_IN_PROGRESS;

		region->count++;
	}

	return EC_RES_SUCCESS;
}

/*
 * Both crec_flash_print_region_into() and crec_flash_response_fill_banks()
 * could be easily implemented if we had an access to flash layout structure
 * that aggregates pages with the same size in one entry ('compressed' form).
 *
 * Zephyr internally keeps flash layout in structure of this type, but using the
 * flash API it's only possible to get information about single pages. Extending
 * flash API encounters resistance from developers.
 */
void crec_flash_print_region_info(void)
{
	const struct flash_parameters *params;
	struct ec_flash_bank region;
	size_t sector_idx = 0;
	int res;

	params = flash_get_parameters(flash_ctrl_dev);
	if (!params)
		return;

	cprintf(CC_COMMAND, "Regions:\n");
	do {
		res = flash_get_region(sector_idx, &region);
		if (res != EC_RES_SUCCESS && res != EC_RES_IN_PROGRESS)
			break;

		cprintf(CC_COMMAND, " %d region%s:\n", region.count,
			(region.count == 1 ? "" : "s"));
		cprintf(CC_COMMAND, "  Erase:   %4d B (to %d-bits)\n",
			1 << region.erase_size_exp,
			params->erase_value ? 1 : 0);
		cprintf(CC_COMMAND, "  Size/Protect: %4d B\n",
			1 << region.size_exp);

		sector_idx += region.count;
	} while (res == EC_RES_IN_PROGRESS);
}

int crec_flash_response_fill_banks(struct ec_response_flash_info_2 *r,
				   int num_banks)
{
	struct ec_flash_bank region;
	size_t sector_idx = 0;
	int banks_idx = 0;
	int res;

	do {
		res = flash_get_region(sector_idx, &region);
		if (res != EC_RES_SUCCESS && res != EC_RES_IN_PROGRESS)
			break;

		if (banks_idx < num_banks)
			memcpy(&r->banks[banks_idx], &region,
			       sizeof(struct ec_flash_bank));

		sector_idx += region.count;
		banks_idx++;
	} while (res == EC_RES_IN_PROGRESS);

	r->num_banks_desc = MIN(banks_idx, num_banks);
	r->num_banks_total = banks_idx;

	return res;
}

int crec_flash_total_banks(void)
{
	return flash_get_page_count(flash_ctrl_dev);
}
#endif /* CONFIG_PLATFORM_EC_USE_ZEPHYR_FLASH_PAGE_LAYOUT */

#ifdef CONFIG_PLATFORM_EC_SHARED_SPI_FLASH
#define DT_DRV_COMPAT cros_ec_shared_spi_flash

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
	     "Unsupported External SPI GPIO");

const struct gpio_dt_spec spi_oe =
	GPIO_DT_SPEC_GET(DT_INST(0, DT_DRV_COMPAT), spi_oe_gpios);

static void flash_shared_enable_ec_access(void)
{
	/* EC to get access to SPI flash  */
	gpio_pin_set_dt(&spi_oe, 0);
	/* delay before EC access the external SPI flash */
	k_msleep(10);
}
DECLARE_HOOK(HOOK_SYSJUMP, flash_shared_enable_ec_access, HOOK_PRIO_FIRST);

static void flash_shared_enable_ap_access(void)
{
	/* AP to get access to SPI flash */
	gpio_pin_set_dt(&spi_oe, 1);
}
DECLARE_HOOK(HOOK_INIT, flash_shared_enable_ap_access, HOOK_PRIO_FIRST);
#endif /* CONFIG_PLATFORM_EC_SHARED_SPI_FLASH */

#if IS_ENABLED(CONFIG_SHELL)
static int command_flashchip(const struct shell *shell, size_t argc,
			     char **argv)
{
	uint8_t manufacturer;
	uint16_t device;
	uint8_t status1;
	uint8_t status2;
	int res;

	res = cros_flash_physical_get_status(cros_flash_dev, &status1,
					     &status2);

	if (!res)
		shell_fprintf(shell, SHELL_NORMAL,
			      "Status 1: 0x%02x, Status 2: 0x%02x\n", status1,
			      status2);

	res = cros_flash_physical_get_jedec_id(cros_flash_dev, &manufacturer,
					       &device);

	if (!res)
		shell_fprintf(shell, SHELL_NORMAL,
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
#if IS_ENABLED(CONFIG_SOC_FAMILY_NPCX)
#if CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY <= \
	CONFIG_CROS_FLASH_NPCX_INIT_PRIORITY
#error "CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY must be greater than" \
	"CONFIG_CROS_FLASH_NPCX_INIT_PRIORITY."
#endif
#elif IS_ENABLED(CONFIG_SOC_FAMILY_MEC)
#if CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY <= \
	CONFIG_CROS_FLASH_MCHP_INIT_PRIORITY
#error "CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY must be greater than" \
	"CONFIG_CROS_FLASH_MCHP_INIT_PRIORITY."
#endif
#elif IS_ENABLED(CONFIG_CROS_FLASH)
#if CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY <= CONFIG_CROS_FLASH_INIT_PRIORITY
#error "CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY must be greater than" \
	"CONFIG_CROS_FLASH_INIT_PRIORITY."
#endif
#endif
SYS_INIT(flash_dev_init, POST_KERNEL, CONFIG_PLATFORM_EC_FLASH_INIT_PRIORITY);
