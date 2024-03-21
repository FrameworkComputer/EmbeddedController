/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yavista sub-board hardware configuration */

#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usbc/usb_muxes.h"
#include "yavista_sub_board.h"

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static uint8_t cached_usb_pd_port_count;

__override uint8_t board_get_usb_pd_port_count(void)
{
	__ASSERT(cached_usb_pd_port_count != 0,
		 "sub-board detection did not run before a port count request");
	if (cached_usb_pd_port_count == 0)
		LOG_WRN("USB PD Port count not initialized!");
	return cached_usb_pd_port_count;
}

test_export_static enum yavista_sub_board_type yavista_cached_sub_board =
	YAVISTA_SB_UNKNOWN;
/*
 * Retrieve sub-board type from FW_CONFIG.
 */
enum yavista_sub_board_type yavista_get_sb_type(void)
{
	int ret;
	uint32_t val;

	/*
	 * Return cached value.
	 */
	if (yavista_cached_sub_board != YAVISTA_SB_UNKNOWN)
		return yavista_cached_sub_board;

	yavista_cached_sub_board = YAVISTA_SB_C_A; /* Defaults to 1A1C */
	ret = cros_cbi_get_fw_config(FW_SUB_BOARD, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d",
			FW_SUB_BOARD);
		return yavista_cached_sub_board;
	}
	switch (val) {
	case FW_SUB_BOARD_1:
		yavista_cached_sub_board = YAVISTA_SB_A;
		LOG_INF("SB: Only USB type A");
		break;
	case FW_SUB_BOARD_2:
		yavista_cached_sub_board = YAVISTA_SB_C_A;
		LOG_INF("SB: USB type C, USB type A");
		break;
	default:
		break;
	}
	return yavista_cached_sub_board;
}

/*
 * Initialise the USB PD port count, which
 * depends on which sub-board is attached.
 */
test_export_static void board_usb_pd_count_init(void)
{
	switch (yavista_get_sb_type()) {
	case YAVISTA_SB_A:
		cached_usb_pd_port_count = 1;
		break;
	default:
		cached_usb_pd_port_count = 2;
		break;
	}
}
/*
 * Make sure setup is done after EEPROM is readable.
 */
DECLARE_HOOK(HOOK_INIT, board_usb_pd_count_init, HOOK_PRIO_INIT_I2C);

/**
 * Configure mux function that vary with present sub-board.
 */
static void yavista_subboard_config(void)
{
	enum yavista_sub_board_type sb = yavista_get_sb_type();

	if (sb == YAVISTA_SB_A) {
		/* Port doesn't exist, doesn't need muxing */
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_1_no_mux);
	}
}
DECLARE_HOOK(HOOK_INIT, yavista_subboard_config, HOOK_PRIO_POST_FIRST);

/*
 * Enable interrupts
 */
static void board_init(void)
{
	/*
	 * Enable USB-C interrupts.
	 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0));
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
	if (board_get_usb_pd_port_count() == 2)
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1));
#endif
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
