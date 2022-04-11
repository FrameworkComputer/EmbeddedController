/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "fw_config.h"
#include "gpio.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

static union taniks_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/*
 * FW_CONFIG defaults for Taniks if the CBI.FW_CONFIG data is not
 * initialized.
 */
static const union taniks_cbi_fw_config fw_config_defaults = {
	.usb_db = DB_USB3_PS8815,
	.kb_bl = KEYBOARD_BACKLIGHT_ENABLED,
};

/****************************************************************************
 * Taniks FW_CONFIG access
 */
static void determine_storage(void)
{
	const bool has_nvme = fw_config.nvme_status == NVME_ENABLED;
	const bool has_emmc = fw_config.emmc_status == EMMC_ENABLED;
	/*
	 * If both masks are enabled or disabled, read the EMMC_SKU_DET pin
	 * (should happen only in the factory).
	 */
	if (has_nvme == has_emmc) {
		/* 0 = eMMC SKU, 1 = NVMe SKU */
		if (gpio_get_level(GPIO_EMMC_SKU_DET)) {
			CPRINTS("CBI: Detected NVMe SKU, disabling eMMC");
			fw_config.emmc_status = EMMC_DISABLED;
			fw_config.nvme_status = NVME_ENABLED;
		} else {
			CPRINTS("CBI: Detected eMMC SKU, disabling NVMe");
			fw_config.nvme_status = NVME_DISABLED;
			fw_config.emmc_status = EMMC_ENABLED;
		}
	}
	cbi_set_board_info(CBI_TAG_FW_CONFIG, (uint8_t *)&fw_config,
			   sizeof(fw_config));
}

void board_init_fw_config(void)
{
	if (cbi_get_fw_config(&fw_config.raw_value)) {
		CPRINTS("CBI: Read FW_CONFIG failed, using board defaults");
		fw_config = fw_config_defaults;
	}

	if (get_board_id() == 0) {
		/* TODO(b/211076082): Update CBI fw config structure
		 * Update correct FW_CONFIG.
		 */
		CPRINTS("CBI: Using board defaults for early board");
		if (ec_cfg_has_tabletmode()) {
			fw_config = fw_config_defaults;
		} 
	}

	determine_storage();
}

union taniks_cbi_fw_config get_fw_config(void)
{
	return fw_config;
}

enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void)
{
	return fw_config.usb_db;
}

bool ec_cfg_has_keyboard_backlight(void)
{
	return (fw_config.kb_bl == KEYBOARD_BACKLIGHT_ENABLED);
}

bool ec_cfg_has_tabletmode(void)
{
	return (fw_config.tabletmode == TABLETMODE_ENABLED);
}
