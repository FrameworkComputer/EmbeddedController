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

static union osiris_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/*
 * FW_CONFIG defaults for osiris if the CBI.FW_CONFIG data is not
 * initialized.
 */
static const union osiris_cbi_fw_config fw_config_defaults = {
	.kb_bl = KEYBOARD_BACKLIGHT_ENABLED,
};

/****************************************************************************
 * Osiris FW_CONFIG access
 */
void board_init_fw_config(void)
{
	if (cbi_get_fw_config(&fw_config.raw_value)) {
		CPRINTS("CBI: Read FW_CONFIG failed, using board defaults");
		fw_config = fw_config_defaults;
	}
}

union osiris_cbi_fw_config get_fw_config(void)
{
	return fw_config;
}
