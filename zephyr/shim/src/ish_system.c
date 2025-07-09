/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "power.h"
#include "system.h"

int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ON;
}

test_mockable const char *system_get_chip_vendor(void)
{
	return "Intel";
}

test_mockable const char *system_get_chip_name(void)
{
	return "Intel x86";
}

test_mockable const char *system_get_chip_revision(void)
{
	return "";
}

test_mockable int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	/* This is not applied to ISH, keep to pass build */
	return EC_ERROR_UNIMPLEMENTED;
}

/*
 * CONFIG_PLATFORM_EC_FLASH_CROS and CONFIG_EC_PROTECTED_STORAGE_OFF
 * are not applicable to ISH.
 * Below functions are added to pass build requirement.
 * However do not include this block if CONFIG_ZTEST is enabled; to
 * avoid duplicate definitions which are in ztest_system.c too.
 */
#if !defined(CONFIG_ZTEST)
#ifndef CONFIG_PLATFORM_EC_FLASH_CROS
int crec_flash_read(int offset, int size, char *data)
{
	return EC_ERROR_INVAL;
}

uint32_t crec_flash_get_protect(void)
{
	return 0;
}
#endif

#ifndef CONFIG_EC_PROTECTED_STORAGE_OFF
uint32_t flash_get_rw_offset(enum ec_image copy)
{
	return 0;
}
#endif
#endif /* CONFIG_ZTEST */

test_mockable int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	/* This is not applied to ISH, keep to pass build */
	return EC_ERROR_UNIMPLEMENTED;
}

extern void sedi_pm_reset(void);

test_mockable void system_reset(int flags)
{
	if (IS_ENABLED(CONFIG_REBOOT) && IS_ENABLED(CONFIG_PM)) {
		sedi_pm_reset();
	}
	__builtin_unreachable();
}

test_mockable void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* This is not applied to ISH, keep to pass build */
}

int system_get_scratchpad(uint32_t *value)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int system_set_scratchpad(uint32_t value)
{
	return EC_ERROR_UNIMPLEMENTED;
}

void chip_save_reset_flags(uint32_t flags)
{
	/* TODO: will add implementation later */
}

uint32_t chip_read_reset_flags(void)
{
	/* TODO: will add implementation later */
	return EC_RESET_FLAG_POWER_ON;
}

test_export_static int system_preinitialize(void)
{
	system_set_reset_flags(chip_read_reset_flags());
	return 0;
}

SYS_INIT(system_preinitialize, PRE_KERNEL_1,
	 CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY);
