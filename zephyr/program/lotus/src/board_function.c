/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "board_host_command.h"
#include "board_function.h"
#include "chipset.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "diagnostics.h"
#include "ec_commands.h"
#include "extpower.h"
#include "flash_storage.h"
#include "gpio/gpio_int.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "util.h"
#include "zephyr_console_shim.h"

#ifdef CONFIG_BOARD_LOTUS
#include "input_module.h"
#endif

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ##args)

/* counter for chassis open while ec no power, only rtc power */
static uint8_t chassis_vtr_open_count;
/* counter for chassis open while ec has power */
static uint8_t chassis_open_count;
/* counter for chassis press while ec has power, clear when enter S0 */
static uint8_t chassis_press_counter;
/* make sure only trigger once */
static uint8_t chassis_once_flag;

static uint64_t chassis_open_hibernate_time;

int bios_function_status(uint16_t type, uint16_t addr, uint8_t flag)
{
	uint8_t status;

	switch (type) {
	case TYPE_MEMMAP:
		status = (*host_get_memmap(addr) & flag) ? true : false;
		break;
	case TYPE_BBRAM:
		system_get_bbram(addr, &status);
		break;
	case TYPE_FLASH:
		status = flash_storage_get(addr);
		break;
	}
	return status;
}

/*
 * Configure the AP boot up function
 */
int ac_boot_status(void)
{
	return bios_function_status(TYPE_MEMMAP, EC_CUSTOMIZED_MEMMAP_BIOS_SETUP_FUNC,
		EC_AC_ATTACH_BOOT);
}

void bios_function_detect(void)
{
	system_set_bbram(SYSTEM_BBRAM_IDX_BIOS_FUNCTION, ac_boot_status());

	flash_storage_update(FLASH_FLAGS_STANDALONE, get_standalone_mode() ? 1 : 0);
#ifdef CONFIG_BOARD_LOTUS
	flash_storage_update(FLASH_FLAGS_INPUT_MODULE_POWER, get_detect_mode());
#endif
	flash_storage_commit();
}

int chassis_cmd_clear(int type)
{
	int press;

	if (type) {
		/* clear when host cmd send magic value */
		chassis_vtr_open_count = 0;
		chassis_open_count = 0;
	} else {
		/* clear when bios get, bios will get this data while post */
		press = chassis_press_counter;
		chassis_press_counter = 0;
		return press;
	}
	return -1;
}

static void chassis_open_hibernate(void)
{
	uint64_t now;
	int chassis_status = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l));

	/* We don't need to hibernate EC when extpower is present or chassis is closed */
	if (extpower_is_present() || chassis_status || !chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return;

	/* EC does not update the chassis open hibernate timer, ignore it */
	if (!chassis_open_hibernate_time)
		return;

	now = get_time().val;
	CPRINTS("chassis_open_hibernate_time:%lld, now:%lld", chassis_open_hibernate_time, now);
	if (now > chassis_open_hibernate_time) {
		CPRINTS("Chassis open hibernate");
		system_hibernate(0, 0);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, chassis_open_hibernate, HOOK_PRIO_DEFAULT);
DECLARE_DEFERRED(chassis_open_hibernate);

__override enum critical_shutdown
board_system_is_idle(uint64_t last_shutdown_time, uint64_t *target,
		     uint64_t now)
{
	/* update the chassis open target time = 30s - 28s*/
	chassis_open_hibernate_time = *target - 28000000;

	/* After setting the chassis open hibernate timer, delay 2.5s to check the chassis status */
	hook_call_deferred(&chassis_open_hibernate_data, 2500 * MSEC);

	CPRINTS("target:%lld, now:%lld", *target, now);

	if (now < *target)
		return CRITICAL_SHUTDOWN_IGNORE;

	CPRINTS("SDC Safe");
	return CRITICAL_SHUTDOWN_HIBERNATE;
}

__overridable void project_chassis_function(enum gpio_signal signal)
{
}

static void check_chassis_open(void)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l)) == 0
			&& !chassis_once_flag) {

		CPRINTS("Chassis was opened");
		chassis_once_flag = 1;

		/* Record the chassis was open status in bbram */
		system_set_bbram(SYSTEM_BBRAM_IDX_CHASSIS_WAS_OPEN, 1);

		/* Counter for chasis pin */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			if (chassis_press_counter < 0xFF)
				chassis_press_counter++;
	} else if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l)) == 1
			&& chassis_once_flag) {

		CPRINTS("Chassis was closed");
		chassis_once_flag = 0;
	}

	hook_call_deferred(&chassis_open_hibernate_data, 0);
}
DECLARE_DEFERRED(check_chassis_open);

void chassis_interrupt_handler(enum gpio_signal signal)
{
	project_chassis_function(signal);
	hook_call_deferred(&check_chassis_open_data, 50 * MSEC);
}

static void bios_function_init(void)
{
	if (!ac_boot_status())
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BIOS_SETUP_FUNC) =
			bios_function_status(TYPE_BBRAM, SYSTEM_BBRAM_IDX_BIOS_FUNCTION, 0);

	if (flash_storage_get(FLASH_FLAGS_STANDALONE))
		set_standalone_mode(1);
#ifdef CONFIG_BOARD_LOTUS
	set_detect_mode(flash_storage_get(FLASH_FLAGS_INPUT_MODULE_POWER));
#endif
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_chassis_open));
	check_chassis_open();
}
DECLARE_HOOK(HOOK_INIT, bios_function_init, HOOK_PRIO_DEFAULT + 1);