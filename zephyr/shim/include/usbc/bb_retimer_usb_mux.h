/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H
#define __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H

#include "driver/retimer/bb_retimer_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB
#define BB_RETIMER_USB_MUX_COMPAT intel_jhl8040r
#elif CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_HB
#define BB_RETIMER_USB_MUX_COMPAT intel_jhl9040r
#endif

/* clang-format off */
#define USB_MUX_CONFIG_BB_RETIMER(mux_id)              \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &bb_usb_retimer,             \
		.hpd_update = bb_retimer_hpd_update,   \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}
/* clang-format on */

/**
 * @brief Get reset gpio for @p mux_id retimer
 *
 * @param mux_id BB retimer DTS node
 */
#define BB_RETIMER_RESET_GPIO(mux_id) GPIO_SIGNAL(DT_PHANDLE(mux_id, reset_pin))

/**
 * @brief Get LS_EN gpio for @p mux_id retimer
 *
 * @param mux_id BB retimer DTS node
 */
#define BB_RETIMER_LS_EN_GPIO(mux_id)                             \
	COND_CODE_1(DT_NODE_HAS_PROP(mux_id, ls_en_pin),          \
		    (GPIO_SIGNAL(DT_PHANDLE(mux_id, ls_en_pin))), \
		    (GPIO_UNIMPLEMENTED))

#define BB_RETIMER_CONTROLS_CONFIG(mux_id)                         \
	{                                                          \
		.retimer_rst_gpio = BB_RETIMER_RESET_GPIO(mux_id), \
		.usb_ls_en_gpio = BB_RETIMER_LS_EN_GPIO(mux_id),   \
	}

/**
 * @brief Set entry in bb_controls array
 *
 * @param mux_id BB retimer node ID
 */
#define USB_MUX_BB_RETIMER_CONTROL_ARRAY(mux_id) \
	[USB_MUX_PORT(mux_id)] = BB_RETIMER_CONTROLS_CONFIG(mux_id),

/**
 * @brief Call USB_MUX_BB_RETIMER_CONTROL_ARRAY for every BB retimer in DTS
 */
#define USB_MUX_BB_RETIMERS_CONTROLS_ARRAY                \
	DT_FOREACH_STATUS_OKAY(BB_RETIMER_USB_MUX_COMPAT, \
			       USB_MUX_BB_RETIMER_CONTROL_ARRAY)

/**
 * @brief Check if BB retimers @p id_1 and @p id_2 has matching configuration
 *        Configuration match if reset and ls_en pins are the same for muxes
 *        which are on the same USB-C port.
 *
 * @param id_1 First BB retimer DTS node
 * @param id_2 Second BB retimer DTS node
 */
#define BB_RETIMER_CHECK_PAIR(id_1, id_2)                                      \
	BUILD_ASSERT(USB_MUX_PORT(id_1) != USB_MUX_PORT(id_2) ||               \
			     (BB_RETIMER_RESET_GPIO(id_1) ==                   \
				      BB_RETIMER_RESET_GPIO(id_2) &&           \
			      BB_RETIMER_LS_EN_GPIO(id_1) ==                   \
				      BB_RETIMER_LS_EN_GPIO(id_2)),            \
		     "BB retimers " #id_1 " and " #id_2 " have different pin " \
		     "configuration and same USB-C port")

/**
 * @brief Check if BB retimers with @p inst instance number has matching
 *        configuration with muxes of higher instance number on @p bb_list list.
 *        Configuration match if reset and ls_en pins are the same for muxes
 *        which are on the same USB-C port.
 *
 * @param inst Instance number of BB retimer mux
 * @param bb_list List of BB retimers in instance number order
 */
#define BB_RETIMER_CHECK_INSTANCE_WITH_LIST(inst, bb_list)           \
	FOR_EACH_FIXED_ARG(BB_RETIMER_CHECK_PAIR, (;),               \
			   DT_INST(inst, BB_RETIMER_USB_MUX_COMPAT), \
			   GET_ARGS_LESS_N(inst, __DEBRACKET bb_list))

/**
 * @brief Check if BB retimers on the @p bb_list list have matching
 *        configurations (i.e. reset and ls_en pins are the same for muxes
 *        which are on the same USB-C port). This check is required, because
 *        USB_MUX_ENABLE_ALTERNATE() doesn't update bb_control[] array, so all
 *        BB retimers needs to use the same GPIO pins.
 *
 * @param bb_list List of BB retimers in instance number order
 */
#define BB_RETIMER_CHECK_SAME_CONTROLS(bb_list)                     \
	LISTIFY(DT_NUM_INST_STATUS_OKAY(BB_RETIMER_USB_MUX_COMPAT), \
		BB_RETIMER_CHECK_INSTANCE_WITH_LIST, (;), bb_list);

/** List of all BB retimers in DTS in instance number order */
#define BB_RETIMER_INSTANCES_LIST                                             \
	(LISTIFY(DT_NUM_INST_STATUS_OKAY(BB_RETIMER_USB_MUX_COMPAT), DT_INST, \
		 (, ), BB_RETIMER_USB_MUX_COMPAT))

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_BB_RETIMER_USB_MUX_H */
