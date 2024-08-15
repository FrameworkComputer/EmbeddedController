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
#include "power_button.h"
#include "system.h"
#include "temp_sensor.h"
#include "util.h"
#include "zephyr_console_shim.h"

#ifdef CONFIG_BOARD_LOTUS
#include "input_module.h"
#endif

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ##args)

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

static int power_button_state;
static void power_button_signal_deferred(void);
DECLARE_DEFERRED(power_button_signal_deferred);

static void power_button_signal_deferred(void)
{
#if DT_NODE_EXISTS(DT_ALIAS(gpio_check_fp_control))

	static timestamp_t stime;

	if (!stime.val)
		stime = get_time();

	/**
	 * Ignore the power button signal when fp control enable
	 *
	 * If user removes the fingerprint module, the fp control signal is always high.
	 * Only enable this feature when the system does not enable the standalone mode,
	 * and system state in S0.
	 */
	if (gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_check_fp_control)) &&
		chipset_in_state(CHIPSET_STATE_ON) && !get_standalone_mode()) {
		if ((get_time().val < stime.val + 4 * SECOND) && !power_button_state) {
			hook_call_deferred(&power_button_signal_deferred_data, 100 * MSEC);
			return;
		} else if ((get_time().val > stime.val + 4 * SECOND) && !power_button_state)
			chipset_force_shutdown(CHIPSET_SHUTDOWN_BOARD_CUSTOM);
		else
			stime.val = 0;
	}

	stime.val = 0;
#endif
	/* power_button_interrupt may not use the signal variant, so always set 0 */
	power_button_interrupt(0);
}

void board_power_button_interrupt(enum gpio_signal signal)
{
	power_button_state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_on_off_btn_l));
	hook_call_deferred(&power_button_signal_deferred_data, 50);
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
}
DECLARE_HOOK(HOOK_INIT, bios_function_init, HOOK_PRIO_DEFAULT + 1);

/*
 * at Lotus and azalea all temps sensor power source reference SLP_S3
 * so don't read any temps sensor when power not ready.
 */
__override int board_temp_smi_evet(void)
{
	/*
	 * we don't send thermal smi event to host, if need any
	 * printing, add log and condition in here, don't need
	 * return true.
	 */

	return false;
}
