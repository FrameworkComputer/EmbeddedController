/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Teliks sub-board hardware configuration */

#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "task.h"
#include "teliks_sub_board.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static uint8_t cached_usb_pd_port_count;

__override uint8_t board_get_usb_pd_port_count(void)
{
	__ASSERT(cached_usb_pd_port_count != 0,
		 "sub-board detection did not run before a port count request");
	/* LCOV_EXCL_START - will not happen due to board_init is
	 * HOOK_PRIO_DEFAULT
	 */
	if (cached_usb_pd_port_count == 0)
		LOG_WRN("USB PD Port count not initialized!");
	/* LCOV_EXCL_STOP */
	return cached_usb_pd_port_count;
}

test_export_static enum teliks_sub_board_type teliks_cached_sub_board =
	TELIKS_SB_UNKNOWN;
/*
 * Retrieve sub-board type from FW_CONFIG.
 */
enum teliks_sub_board_type teliks_get_sb_type(void)
{
	int ret;
	uint32_t val;

	/*
	 * Return cached value.
	 */
	if (teliks_cached_sub_board != TELIKS_SB_UNKNOWN)
		return teliks_cached_sub_board;

	teliks_cached_sub_board = TELIKS_SB_NONE; /* Defaults to none */
	ret = cros_cbi_get_fw_config(FW_SUB_BOARD, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d",
			FW_SUB_BOARD);
		return teliks_cached_sub_board;
	}

	switch (val) {
	case FW_SUB_BOARD_1:
		teliks_cached_sub_board = TELIKS_SB_C;
		LOG_INF("SB: USB type C");
		break;
	default:
		break;
	}
	return teliks_cached_sub_board;
}

/*
 * Initialise the USB PD port count, which
 * depends on which sub-board is attached.
 */
test_export_static void board_usb_pd_count_init(void)
{
	switch (teliks_get_sb_type()) {
	case TELIKS_SB_C:
		cached_usb_pd_port_count = 2;
		break;
	default:
		cached_usb_pd_port_count = 1;
		break;
	}
}
/*
 * Make sure setup is done after EEPROM is readable.
 */
DECLARE_HOOK(HOOK_INIT, board_usb_pd_count_init, HOOK_PRIO_INIT_I2C);

/**
 * Configure GPIOs (and other pin functions) that vary with present sub-board.
 */
static void teliks_subboard_config(void)
{
	enum teliks_sub_board_type sb = teliks_get_sb_type();

	if (sb != TELIKS_SB_C) {
		/* Port doesn't exist, doesn't need muxing */
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_1_no_mux);
	}
}
DECLARE_HOOK(HOOK_INIT, teliks_subboard_config, HOOK_PRIO_POST_FIRST);

/*
 * Enable interrupts
 */
static void board_init(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_s5), 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
