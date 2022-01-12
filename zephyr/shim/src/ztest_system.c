/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "cros_version.h"
#include "battery.h"
#include "charge_manager.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Ongoing actions preventing going into deep-sleep mode. */
atomic_t sleep_mask;

void system_common_pre_init(void)
{
}

int system_add_jump_tag(uint16_t tag, int version, int size, const void *data)
{
	return EC_SUCCESS;
}

const uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size)
{
	return NULL;
}

#ifdef CONFIG_ZTEST
struct system_jumped_late_mock system_jumped_late_mock = {
	.ret_val = 0,
	.call_count = 0,
};
#endif

int system_jumped_late(void)
{
#ifdef CONFIG_ZTEST
	system_jumped_late_mock.call_count++;

	return system_jumped_late_mock.ret_val;
#else
	return 0;
#endif
}

enum ec_image system_get_image_copy(void)
{
	return EC_IMAGE_RW;
}

int system_is_locked(void)
{
	return 0;
}

int system_is_in_rw(void)
{
	return 1;
}

uint32_t system_get_reset_flags(void)
{
	return 0;
}

void system_print_banner(void)
{
	printk("Image: %s\n", build_info);
}

void system_set_reset_flags(uint32_t flags)
{
}

struct jump_data *get_jump_data(void)
{
	return NULL;
}

__attribute__((weak))
void system_reset(int flags)
{
	__builtin_unreachable();
}

int system_can_boot_ap(void)
{
	int soc = -1;
	int pow = -1;

#if defined(CONFIG_BATTERY) && \
	defined(CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON)
	/* Require a minimum battery level to power on. If battery isn't
	 * present, battery_state_of_charge_abs returns false.
	 */
	if (battery_state_of_charge_abs(&soc) == EC_SUCCESS &&
			soc >= CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON)
		return 1;
#endif

#if defined(CONFIG_CHARGE_MANAGER) && \
	defined(CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON)
	pow = charge_manager_get_power_limit_uw() / 1000;
	if (pow >= CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON)
		return 1;
#else
	/* For fixed AC system */
	return 1;
#endif

	CPRINTS("Not enough power to boot (%d %%, %d mW)", soc, pow);
	return 0;
}
