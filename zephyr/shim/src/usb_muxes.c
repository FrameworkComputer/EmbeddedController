/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/sys/util_macro.h>
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

/**
 * @brief Macro that can be used in USB_MUX_FOREACH_USBC_PORT as filter
 *        argument. It allows to evaluate to "1 ||" for each named USBC port
 *        that has usb-muxes property.
 */
#define USB_MUX_PORT_HAS_MUX(unused1, unused2) 1 ||

/**
 * Check if there is any named USBC port with usb-muxes property. It evaluates
 * to "1 || 1 || ... 1 || 0" when there are multiple named USBC ports with
 * usb-muxes property and to "0" when any named USBC port has usb-muxes
 * property.
 *
 * This prevents creating struct usb_mux usb_muxes[] for platforms that didn't
 * migrate USB mux configuration to DTS yet.
 */
#if USB_MUX_FOREACH_USBC_PORT(USB_MUX_PORT_HAS_MUX, _) 0

/**
 * Define root of each USB muxes chain e.g.
 * [0] = {
 *         .usb_port = 0,
 *         .next_mux = &USB_MUX_NODE_DT_N_S_usbc_S_port0_0_S_it5205_mux_0,
 *         .board_init = &board_init,
 *         .board_set = NULL,
 *         .flags = 0,
 *         .driver = &virtual_usb_mux_driver,
 *         .hpd_update = &virtual_hpd_update,
 * },
 * [1] = { ... },
 */
MAYBE_CONST struct usb_mux usb_muxes[] = { USB_MUX_FOREACH_USBC_PORT(
	USB_MUX_FIRST, USB_MUX_ARRAY) };

/**
 * Define all USB muxes except roots e.g.
 * MAYBE_CONST struct usb_mux USB_MUX_NODE_DT_N_S_usbc_S_port0_0_S_mux_0 = {
 *         .usb_port = 0,
 *         .next_mux = NULL,
 *         .board_init = NULL,
 *         .board_set = NULL,
 *         .flags = 0,
 *         .driver = &virtual_usb_mux_driver,
 *         .hpd_update = &virtual_hpd_update,
 * };
 * MAYBE_CONST struct usb_mux USB_MUX_NODE_<node_id> = { ... };
 */
USB_MUX_FOREACH_USBC_PORT(USB_MUX_NO_FIRST, USB_MUX_DEFINE)

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
	USB_MUX_FOREACH_USBC_PORT(USB_MUX_BB_RETIMERS, USB_MUX_ARRAY)
};
#endif /* CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB/HB */

#endif /* #if USB_MUX_FOREACH_USBC_PORT(USB_MUX_PORT_HAS_MUX, _) */
