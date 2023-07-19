/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_transfer.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"

static void cros_cbi_ec_init(void)
{
	if (IS_ENABLED(CONFIG_PLATFORM_EC_CBI_TRANSFER_EEPROM_FLASH)) {
		cros_cbi_transfer_eeprom_to_flash();
	}
	cros_cbi_ssfc_init();
	cros_cbi_fw_config_init();
}

DECLARE_HOOK(HOOK_INIT, cros_cbi_ec_init, HOOK_PRIO_FIRST);
