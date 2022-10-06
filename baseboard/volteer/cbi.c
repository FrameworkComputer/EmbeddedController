/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific functions, shared with Zephyr */

#include "cbi_ec_fw_config.h"
#include "common.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

static uint8_t board_id;

uint8_t get_board_id(void)
{
	return board_id;
}

__overridable void board_cbi_init(void)
{
}

/*
 * Read CBI from i2c eeprom and initialize variables for board variants
 *
 * Example for configuring for a USB3 DB:
 *   ectool cbi set 6 2 4 10
 */
static void cbi_init(void)
{
	uint32_t cbi_val;

	/* Board ID */
	if (cbi_get_board_version(&cbi_val) != EC_SUCCESS ||
	    cbi_val > UINT8_MAX)
		CPRINTS("CBI: Read Board ID failed");
	else
		board_id = cbi_val;

	CPRINTS("Board ID: %d", board_id);

	/* FW config */
	init_fw_config();

	/* Allow the board project to make runtime changes based on CBI data */
	board_cbi_init();
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_FIRST);
