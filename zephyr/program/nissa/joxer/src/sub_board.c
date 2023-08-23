/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Joxer sub-board hardware configuration */

#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "joxer_sub_board.h"
#include "usbc/usb_muxes.h"

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

test_export_static enum joxer_sub_board_type joxer_cached_sub_board =
	JOXER_SB_UNKNOWN;
/*
 * Retrieve sub-board type from FW_CONFIG.
 */
enum joxer_sub_board_type joxer_get_sb_type(void)
{
	int ret;
	uint32_t val;

	/*
	 * Return cached value.
	 */
	if (joxer_cached_sub_board != JOXER_SB_UNKNOWN)
		return joxer_cached_sub_board;

	joxer_cached_sub_board = JOXER_SB_UNKNOWN; /* Defaults to none */
	ret = cros_cbi_get_fw_config(FW_SUB_BOARD, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d",
			FW_SUB_BOARD);
		return joxer_cached_sub_board;
	}

	switch (val) {
	case FW_SUB_BOARD_1:
		joxer_cached_sub_board = JOXER_SB;
		LOG_INF("SB: without USB type C or type A");
		break;
	case FW_SUB_BOARD_2:
		joxer_cached_sub_board = JOXER_SB_C;
		LOG_INF("SB: USB type C");
		break;
	default:
		break;
	}
	return joxer_cached_sub_board;
}

/*
 * Initialise the USB PD port count, which
 * depends on which sub-board is attached.
 */
test_export_static void board_usb_pd_count_init(void)
{
	switch (joxer_get_sb_type()) {
	case JOXER_SB:
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
static void joxer_subboard_config(void)
{
	enum joxer_sub_board_type sb = joxer_get_sb_type();

	if (sb == JOXER_SB) {
		/* Port doesn't exist, doesn't need muxing */
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_1_no_mux);
	}
}
DECLARE_HOOK(HOOK_INIT, joxer_subboard_config, HOOK_PRIO_POST_FIRST);

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
