/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "fw_config.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

static union brask_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/*
 * FW_CONFIG defaults for brask if the CBI.FW_CONFIG data is not
 * initialized.
 */
static const union brask_cbi_fw_config fw_config_defaults = {
	.audio = DB_NAU88L25B_I2S,
	.bj_power = BJ_135W,
};

/*
 * Barrel-jack power adapter ratings.
 */
static const struct {
	int voltage;
	int current;
} bj_power[] = {
	[BJ_150W] = { /* 0 - 150W (also default)*/
			.voltage = 20000,
			.current = 7500
	},
	[BJ_230W] = { /* 1 - 230W */
			.voltage = 19500,
			.current = 11800
	},
	[BJ_65W] = { /* 2 - 65W */
			.voltage = 19000,
			.current = 3420
	},
	[BJ_135W] = { /* 3 - 135W */
			.voltage = 19500,
			.current = 6920
	},
	[BJ_90W] = { /* 4 - 90W */
			.voltage = 19000,
			.current = 4740
	}
};

/****************************************************************************
 * Brask FW_CONFIG access
 */
void board_init_fw_config(void)
{
	if (cbi_get_fw_config(&fw_config.raw_value)) {
		CPRINTS("CBI: Read FW_CONFIG failed, using board defaults");
		fw_config = fw_config_defaults;
	}
}

void ec_bj_power(uint32_t *voltage, uint32_t *current)
{
	unsigned int bj;

	bj = fw_config.bj_power | (fw_config.bj_power_extended << 2);

	/* Out of range value defaults to 0 */
	if (bj >= ARRAY_SIZE(bj_power))
		bj = 0;
	*voltage = bj_power[bj].voltage;
	*current = bj_power[bj].current;
}

bool ec_cfg_has_peripheral_charger(void)
{
	return (fw_config.peripheral_charger == PERIPHERAL_CHARGER_ENABLE);
}

enum conf_mb_usbc_type get_mb_usbc_type(void)
{
	return fw_config.usbc_type;
}
