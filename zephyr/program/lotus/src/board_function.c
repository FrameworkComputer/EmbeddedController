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
#include "ec_commands.h"
#include "extpower.h"
#include "flash_storage.h"
#include "hooks.h"
#include "power_button.h"
#include "system.h"
#include "util.h"
#include "zephyr_console_shim.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ##args)

int bios_function_status(uint16_t type, uint16_t addr, uint8_t flag)
{
	uint8_t status;

	switch (type) {
	case TYPE_NAME:
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
	return bios_function_status(TYPE_NAME, EC_CUSTOMIZED_MEMMAP_BIOS_SETUP_FUNC,
		EC_AC_ATTACH_BOOT);
}

void bios_function_detect(void)
{
	system_set_bbram(SYSTEM_BBRAM_IDX_BIOS_FUNCTION, ac_boot_status());
}

static void bios_function_init(void)
{
	if (!ac_boot_status())
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BIOS_SETUP_FUNC) =
			bios_function_status(TYPE_BBRAM, SYSTEM_BBRAM_IDX_BIOS_FUNCTION, 0);
}
DECLARE_HOOK(HOOK_INIT, bios_function_init, HOOK_PRIO_DEFAULT + 1);

/**
 * Notify PCH of the AC presence.
 */
static void board_extpower(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
		extpower_is_present() && ac_boot_status()) {
		CPRINTS("Power on from boot on AC present");
		power_button_pch_pulse();
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);
