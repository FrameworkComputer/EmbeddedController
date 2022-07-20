/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/sys/util_macro.h>
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

/**
 * This prevents creating struct usb_mux usb_muxes[] for platforms that didn't
 * migrate USB mux configuration to DTS yet.
 */
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_usb_mux_chain)

/**
 * @brief Check if @p mux_id is not part of @p chain_id or if @p chain_id USBC
 *        port is the same as @p mux_port. Result ends with && to construct
 *        logical expression using FOREACH macro.
 *
 * @param chain_id Chain DTS node ID
 * @param mux_id USB mux node ID
 * @param mux_port Port which should be associated with @p mux_id
 */
#define USB_MUX_NOT_IN_CHAIN_OR_PORT_EQ(chain_id, mux_id, mux_port) \
	((USB_MUX_FIND_PORT(chain_id, mux_id)(-1)) == -1 ||         \
	 USBC_PORT(chain_id) == mux_port) &&

/**
 * @brief Check if all chains that contains @p mux_id have the same USB-C port
 *
 * @param mux_id USB mux node ID
 * @param unused_conf Unused argument required by USB_MUX_FOREACH_MUX()
 */
#define USB_MUX_CHECK_ALL_PORTS_ARE_SAME(mux_id, unused_conf)                \
	BUILD_ASSERT(                                                        \
		USB_MUX_FOREACH_CHAIN_VARGS(USB_MUX_NOT_IN_CHAIN_OR_PORT_EQ, \
					    mux_id, USB_MUX_PORT(mux_id)) 1, \
		"USB mux  " #mux_id " is in chains for different ports");

/** Check if for every mux, chains where mux is present have the same port */
USB_MUX_FOREACH_MUX(USB_MUX_CHECK_ALL_PORTS_ARE_SAME)

/**
 * Define usb_mux_chain structures e.g.
 *
 * MAYBE_CONST struct usb_mux_chain
 * USB_MUX_chain_port_<port_id>_mux_<position_id> = {
 *         .mux = &USB_MUX_NODE_DT_N_S_usbc_S_port0_0_S_virtual_mux_0,
 *         .next = &USB_MUX_chain_port_0_mux_1,
 * }
 */
USB_MUX_FOREACH_CHAIN_VARGS(USB_MUX_FOREACH_NO_ROOT_MUX,
			    USB_MUX_CHAIN_STRUCT_DEFINE_OP)

/**
 * Forward declarations for board_init and board_set callbacks. e.g.
 * int c0_mux0_board_init(const struct usb_mux *);
 * int c1_mux0_board_set(const struct usb_mux *, mux_state_t);
 */
USB_MUX_FOREACH_MUX(USB_MUX_CB_BOARD_INIT_DECLARE_IF_EXISTS)
USB_MUX_FOREACH_MUX(USB_MUX_CB_BOARD_SET_DECLARE_IF_EXISTS)

/**
 * Define root of each USB muxes chain e.g.
 * [0] = {
 *         .mux = &USB_MUX_NODE_DT_N_S_usbc_S_port0_0_S_virtual_mux_0,
 *         .next = &USB_MUX_chain_port_0_mux_1,
 * },
 * [1] = { ... },
 */
MAYBE_CONST struct usb_mux_chain usb_muxes[] = { USB_MUX_FOREACH_CHAIN(
	USB_MUX_DEFINE_ROOT_MUX) };
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);

/**
 * Define all USB muxes e.g.
 * MAYBE_CONST struct usb_mux USB_MUX_NODE_DT_N_S_usbc_S_port0_0_S_mux_0 = {
 *         .usb_port = 0,
 *         .board_init = NULL,
 *         .board_set = NULL,
 *         .flags = 0,
 *         .driver = &virtual_usb_mux_driver,
 *         .hpd_update = &virtual_hpd_update,
 * };
 * MAYBE_CONST struct usb_mux USB_MUX_NODE_<node_id> = { ... };
 */
USB_MUX_FOREACH_MUX(USB_MUX_DEFINE)

/* Create bb_controls only if BB or HB retimer driver is enabled */
#if defined(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB) || \
	defined(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_HB)
/**
 * @brief bb_controls array should be constant only if configuration cannot
 *        change in runtime
 */
#define BB_CONTROLS_CONST                                                    \
	COND_CODE_1(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB_RUNTIME_CONFIG, \
		    (), (const))

/**
 * Define bb_controls for BB retimers in USB muxes chain e.g.
 * [0] = {
 *         .retimer_rst_gpio = IOEX_USB_C0_BB_RETIMER_RST,
 *         .usb_ls_en_gpio = IOEX_USB_C0_BB_RETIMER_LS_EN,
 * },
 * [1] = { ... },
 */
BB_CONTROLS_CONST struct bb_usb_control bb_controls[] = {
	USB_MUX_BB_RETIMERS_CONTROLS_ARRAY
};
#endif /* CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB/HB */

#endif /* #if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_usb_mux_chain) */
