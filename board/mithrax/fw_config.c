/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "fw_config.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

static union mithrax_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/*
 * FW_CONFIG defaults for mithrax if the CBI.FW_CONFIG data is not
 * initialized.
 */
static const union mithrax_cbi_fw_config fw_config_defaults = {
	.usb_db = DB_USB3_PS8815,
	.kb_bl = KEYBOARD_BACKLIGHT_ENABLED,
};

/****************************************************************************
 * Mithrax FW_CONFIG access
 */
void board_init_fw_config(void)
{
	if (cbi_get_fw_config(&fw_config.raw_value)) {
		CPRINTS("CBI: Read FW_CONFIG failed, using board defaults");
		fw_config = fw_config_defaults;
	}
}

union mithrax_cbi_fw_config get_fw_config(void)
{
	return fw_config;
}

enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void)
{
	return fw_config.usb_db;
}

enum ec_cfg_usb_mb_type ec_cfg_usb_mb_type(void)
{
	return fw_config.usb_mb;
}

enum ec_cfg_stylus_type ec_cfg_stylus(void)
{
	return fw_config.stylus;
}
