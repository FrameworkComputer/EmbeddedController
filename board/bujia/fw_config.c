/* Copyright 2024 The ChromiumOS Authors
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

static union lisbon_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/*
 * FW_CONFIG defaults for lisbon if the CBI.FW_CONFIG data is not
 * initialized.
 */
static const union lisbon_cbi_fw_config fw_config_defaults = {
	.bj_power = BJ_65W,
	.storage = EMMC,
	.fvm_support = FVM_NO,
};

/*
 * Barrel-jack power adapter ratings.
 */
static const struct {
	int voltage;
	int current;
} bj_power[] = {
	[BJ_65W] = { /* 0 - 65W (also default) */
			.voltage = 19000,
			.current = 3420
	},
	[BJ_90W] = { /* 1 - 90W */
			.voltage = 19000,
			.current = 4740
	}
};

/****************************************************************************
 * Lisbon FW_CONFIG access
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

	bj = fw_config.bj_power;
	/* Out of range value defaults to 0 */
	if (bj >= ARRAY_SIZE(bj_power))
		bj = 0;
	*voltage = bj_power[bj].voltage;
	*current = bj_power[bj].current;
}
