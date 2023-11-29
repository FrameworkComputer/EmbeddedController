/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quandiso sub-board hardware configuration */

#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "quandiso_sub_board.h"
#include "usbc/usb_muxes.h"

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

test_export_static enum quandiso_sub_board_type quandiso_cached_sub_board =
	QUANDISO_SB_UNKNOWN;
/*
 * Retrieve sub-board type from FW_CONFIG.
 */
enum quandiso_sub_board_type quandiso_get_sb_type(void)
{
	int ret;
	uint32_t val;

	/*
	 * Return cached value.
	 */
	if (quandiso_cached_sub_board != QUANDISO_SB_UNKNOWN)
		return quandiso_cached_sub_board;

	quandiso_cached_sub_board = QUANDISO_SB_C_A; /* Defaults to 1A1C */
	ret = cros_cbi_get_fw_config(FW_SUB_BOARD, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d",
			FW_SUB_BOARD);
		return quandiso_cached_sub_board;
	}
	switch (val) {
	case FW_SUB_BOARD_1:
		quandiso_cached_sub_board = QUANDISO_SB_ABSENT;
		LOG_INF("SubBoard: Absent");
		break;
	case FW_SUB_BOARD_2:
		quandiso_cached_sub_board = QUANDISO_SB_C_A;
		LOG_INF("SubBoard: USB type C, USB type A");
		break;
	case FW_SUB_BOARD_3:
		quandiso_cached_sub_board = QUANDISO_SB_LTE;
		LOG_INF("SubBoard: Only LTE");
		break;
	case FW_SUB_BOARD_4:
		quandiso_cached_sub_board = QUANDISO_SB_C_LTE;
		LOG_INF("SubBoard: USB type C + LTE");
		break;
	default:
		break;
	}
	return quandiso_cached_sub_board;
}

/*
 * Initialise the USB PD port count, which
 * depends on which sub-board is attached.
 */
test_export_static void board_usb_pd_count_init(void)
{
	switch (quandiso_get_sb_type()) {
	case QUANDISO_SB_ABSENT:
	case QUANDISO_SB_LTE:
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
static void quandiso_subboard_config(void)
{
	switch (quandiso_get_sb_type()) {
	case QUANDISO_SB_ABSENT:
	case QUANDISO_SB_LTE:
		/* Port doesn't exist, doesn't need muxing */
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_1_no_mux);
		break;
	default:
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, quandiso_subboard_config, HOOK_PRIO_POST_FIRST);

/*
 * Enable interrupts
 */
static void board_tcpc_init(void)
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
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_DEFAULT);
