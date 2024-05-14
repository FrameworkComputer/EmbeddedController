/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHROME_USBC_USB_MUXES_H
#define ZEPHYR_CHROME_USBC_USB_MUXES_H

#include "usb_mux.h"
#include "usbc/amd_fp6_usb_mux.h"
#include "usbc/amd_fp8_usb_mux.h"
#include "usbc/anx7447_usb_mux.h"
#include "usbc/anx7452_usb_mux.h"
#include "usbc/anx7483_usb_mux.h"
#include "usbc/bb_retimer_usb_mux.h"
#include "usbc/it5205_usb_mux.h"
#include "usbc/kb8010_usb_mux.h"
#include "usbc/ps8743_usb_mux.h"
#include "usbc/ps8802_usb_mux.h"
#include "usbc/ps8818_usb_mux.h"
#include "usbc/ps8828_usb_mux.h"
#include "usbc/ps8833_usb_mux.h"
#include "usbc/tcpci_usb_mux.h"
#include "usbc/tusb1064_usb_mux.h"
#include "usbc/utils.h"
#include "usbc/virtual_usb_mux.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/util_macro.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief List of USB mux drivers compatibles and their configurations. Each
 *        element of list has to have (compatible, config) format.
 */
/* clang-format off */
#define USB_MUX_DRIVERS                                         \
	(AMD_FP6_USB_MUX_COMPAT, USB_MUX_CONFIG_AMD_FP6),       \
	(AMD_FP8_USB_MUX_COMPAT, USB_MUX_CONFIG_AMD_FP8),       \
	(ANX7447_USB_MUX_COMPAT, USB_MUX_CONFIG_ANX7447),       \
	(ANX7452_USB_MUX_COMPAT, USB_MUX_CONFIG_ANX7452),       \
	(ANX7483_USB_MUX_COMPAT, USB_MUX_CONFIG_ANX7483),       \
	(BB_RETIMER_USB_MUX_COMPAT, USB_MUX_CONFIG_BB_RETIMER), \
	(IT5205_USB_MUX_COMPAT, USB_MUX_CONFIG_IT5205),         \
	(KB8010_USB_MUX_COMPAT, USB_MUX_CONFIG_KB8010),         \
	(PS8743_USB_MUX_COMPAT, USB_MUX_CONFIG_PS8743),         \
	(PS8743_EMUL_COMPAT, USB_MUX_CONFIG_PS8743),         \
	(PS8802_USB_MUX_COMPAT, USB_MUX_CONFIG_PS8802),         \
	(PS8818_USB_MUX_COMPAT, USB_MUX_CONFIG_PS8818),         \
	(PS8828_USB_MUX_COMPAT, USB_MUX_CONFIG_PS8828),         \
	(PS8833_USB_MUX_COMPAT, USB_MUX_CONFIG_PS8833),         \
	(PS8XXX_USB_MUX_COMPAT, USB_MUX_CONFIG_TCPCI_TCPM),     \
	(TCPCI_TCPM_USB_MUX_COMPAT, USB_MUX_CONFIG_TCPCI_TCPM), \
	(TUSB1064_USB_MUX_COMPAT, USB_MUX_CONFIG_TUSB1064),     \
	(TUSB1064_EMUL_COMPAT, USB_MUX_CONFIG_TUSB1064),        \
	(VIRTUAL_USB_MUX_COMPAT, USB_MUX_CONFIG_VIRTUAL)
/* clang-format on */

/**
 * @brief Name of USB mux chain structure for given port and place in chain.
 *        Note, that root of chain is not referred by this name, but
 *        usb_muxes[@p port_id].
 *
 * @param idx Place in chain
 * @param port_id USBC port id
 */
#define USB_MUX_CHAIN_STRUCT_NAME(idx, port_id) \
	DT_CAT4(USB_MUX_chain_port_, port_id, _mux_, idx)

/**
 * @brief Declaration of USB mux chain structure for @p idx mux in @p port_id
 *        USB-C port's chain
 *
 * @param port_id USBC port ID (number)
 * @param idx Place in chain
 */
#define USB_MUX_CHAIN_STRUCT_DECLARE(port_id, idx) \
	MAYBE_CONST struct usb_mux_chain USB_MUX_CHAIN_STRUCT_NAME(idx, port_id)

/**
 * @brief Name of USB mux structure if node @p mux_id is not EMPTY.
 *
 * @param mux_id USB mux node ID
 */
#define USB_MUX_STRUCT_NAME(mux_id) \
	COND_CODE_0(IS_EMPTY(mux_id), (DT_CAT(USB_MUX_NODE_, mux_id)), (EMPTY))

/**
 * @brief USB muxes in chain should be constant only if configuration
 *        cannot change in runtime
 */
#define MAYBE_CONST \
	COND_CODE_1(CONFIG_PLATFORM_EC_USB_MUX_RUNTIME_CONFIG, (), (const))

/**
 * @brief Declaration of USB mux structure
 *
 * @param mux_id USB mux node ID
 */
#define USB_MUX_STRUCT_DECLARE(mux_id) \
	MAYBE_CONST struct usb_mux USB_MUX_STRUCT_NAME(mux_id)

/**
 * @brief Declaration of USB mux board_init function
 *
 * @param mux_id USB mux node ID
 */
#define USB_MUX_CB_BOARD_INIT_DECLARE(mux_id) \
	int DT_STRING_TOKEN(mux_id, board_init)(const struct usb_mux *);

/**
 * @brief Declaration of USB mux board_set function
 *
 * @param mux_id USB mux node ID
 */
#define USB_MUX_CB_BOARD_SET_DECLARE(mux_id)                           \
	int DT_STRING_TOKEN(mux_id, board_set)(const struct usb_mux *, \
					       mux_state_t);

/**
 * @brief Get pointer by referencing @p name or NULL if @p name is EMPTY
 *
 * @param name Identifier to reference
 */
#define USB_MUX_POINTER_OR_NULL(name) \
	COND_CODE_0(IS_EMPTY(name), (&name), (NULL))

/**
 * @brief Get node id of @p idx USB mux in chain
 *
 * @param chain_id USB mux chain node ID
 * @param idx Position of USB mux in chain
 */
#define USB_MUX_GET_CHAIN_N(chain_id, idx) \
	DT_PHANDLE_BY_IDX(chain_id, usb_muxes, idx)

/**
 * @brief Get next USB mux chain structure name or EMPTY if it is last mux
 *
 * @param chain_id USB mux chain node ID
 * @param idx Position of USB mux in chain
 */
#define USB_MUX_CHAIN_NEXT_NAME(chain_id, idx)                              \
	GET_ARG_N(2,                                                        \
		  GET_ARGS_LESS_N(idx,                                      \
				  LISTIFY(DT_PROP_LEN(chain_id, usb_muxes), \
					  USB_MUX_CHAIN_STRUCT_NAME, (, ),  \
					  USBC_PORT(chain_id))),            \
		  EMPTY)

/**
 * @brief Get pointer to next USB mux in chain or NULL if it is last mux
 *
 * @param chain_id USB mux chain node ID
 * @param idx Position of USB mux in chain
 */
#define USB_MUX_CHAIN_NEXT_POINTER(chain_id, idx) \
	USB_MUX_POINTER_OR_NULL(USB_MUX_CHAIN_NEXT_NAME(chain_id, idx))

/**
 * @brief Get pointer to USB mux that is @p idx in chain @p chain_id
 *
 * @param chain_id USB mux chain node ID
 * @param idx Position of USB mux in chain
 */
#define USB_MUX_POINTER(chain_id, idx) \
	&USB_MUX_STRUCT_NAME(USB_MUX_GET_CHAIN_N(chain_id, idx))

/**
 * @brief Generate pointer to function from @p cb_name property or NULL
 *        if property doesn't exist
 *
 * @param mux_id USB mux node ID
 * @param cb_name Name of property with callback function
 */
#define USB_MUX_CALLBACK_OR_NULL(mux_id, cb_name) \
	USB_MUX_POINTER_OR_NULL(DT_STRING_TOKEN_OR(mux_id, cb_name, EMPTY))

/**
 * @brief Set struct usb_mux fields common for all USB muxes and alter flags
 *
 * @param mux_id USB mux node ID
 * @param flags_mask Mask for bits that should be ignored in flags property
 * @param flags_val Value that should be used instead for masked bits
 */
#define USB_MUX_COMMON_FIELDS_WITH_FLAGS(mux_id, flags_mask, flags_val) \
	.usb_port = USB_MUX_PORT(mux_id),                               \
	.board_init = USB_MUX_CALLBACK_OR_NULL(mux_id, board_init),     \
	.board_set = USB_MUX_CALLBACK_OR_NULL(mux_id, board_set),       \
	.flags = (DT_PROP_OR(mux_id, flags, 0) & ~(flags_mask)) | (flags_val)

/**
 * @brief Set struct usb_mux fields common for all USB muxes
 *
 * @param mux_id USB mux node ID
 */
#define USB_MUX_COMMON_FIELDS(mux_id) \
	USB_MUX_COMMON_FIELDS_WITH_FLAGS(mux_id, 0, 0)

/**
 * @brief Declare USB mux structure
 *
 * @param mux_id USB mux node ID
 * @param conf Driver configuration function
 */
#define USB_MUX_DECLARE(mux_id, conf) extern USB_MUX_STRUCT_DECLARE(mux_id);

/**
 * @brief Define USB mux structure using driver USB_MUX_CONFIG_* macro
 *
 * @param mux_id USB mux node ID
 * @param conf Driver configuration function
 */
#define USB_MUX_DEFINE(mux_id, conf) \
	USB_MUX_STRUCT_DECLARE(mux_id) = conf(mux_id);

/**
 * @brief Call @p cb_op if @p mux_id has @p cb_prop property
 *
 * @param mux_id USB mux node ID
 * @param cb_prop The callback property name
 * @param cb_op Operation to perform on USB muxes
 */
#define USB_MUX_CB_DECLARE_IF_EXIST(mux_id, cb_prop, cb_op) \
	COND_CODE_1(DT_NODE_HAS_PROP(mux_id, cb_prop), (cb_op(mux_id)), ())

/**
 * @brief Declare USB mux board_set function @p mux_id has board_set property
 *
 * @param mux_id USB mux node ID
 * @param conf Driver configuration function
 */
#define USB_MUX_CB_BOARD_SET_DECLARE_IF_EXISTS(mux_id, conf) \
	USB_MUX_CB_DECLARE_IF_EXIST(mux_id, board_set,       \
				    USB_MUX_CB_BOARD_SET_DECLARE)

/**
 * @brief Declare USB mux board_init function @p mux_id has board_init property
 *
 * @param mux_id USB mux node ID
 * @param conf Driver configuration function
 */
#define USB_MUX_CB_BOARD_INIT_DECLARE_IF_EXISTS(mux_id, conf) \
	USB_MUX_CB_DECLARE_IF_EXIST(mux_id, board_init,       \
				    USB_MUX_CB_BOARD_INIT_DECLARE)

/**
 * @brief Call @p op operation for each USB mux node that is compatible with
 *        any driver from the USB_MUX_DRIVERS list.
 *        DT_FOREACH_STATUS_OKAY_VARGS() macro can not be used in @p op
 *
 * @param op Operation to perform on each USB mux. Should accept mux node ID and
 *           driver config as arguments.
 */
#define USB_MUX_FOREACH_MUX_DT_VARGS(op) \
	DT_FOREACH_USBC_DRIVER_STATUS_OK_VARGS(op, USB_MUX_DRIVERS)

/**
 * @brief Convert @p mux_id and @p conf pair into USB_MUX_LIST entry
 *
 * @param mux_id USB mux node ID
 * @param conf Driver configuration function
 */
#define USB_MUX_TO_LIST(mux_id, conf) , (mux_id, conf)

/**
 * @brief List of all USB muxes with config matched by compatible. List is in
 *        format (mux1_id, conf1) , (mux2_id, conf2) ...
 */
#define USB_MUX_LIST \
	LIST_DROP_EMPTY(USB_MUX_FOREACH_MUX_DT_VARGS(USB_MUX_TO_LIST))

/**
 * @brief Call @p op with @p args arguments
 *
 * @param op Operation to perform on USB mux. Should accept mux node ID and
 *           driver config as arguments.
 * @param args Arguments for @p op. Should be in format (mux_id, conf).
 */
#define USB_MUX_CALL_OP(args, op) op args

/**
 * @brief Call @p op operation for each USB mux node from USB_MUX_LIST. This is
 *        like USB_MUX_FOREACH_MUX_DT_VARGS(), except
 *        DT_FOREACH_STATUS_OKAY_VARGS() macro can be used in @p op
 *
 * @param op Operation to perform on each USB mux. Should accept mux node ID and
 *           driver config as arguments.
 */
#define USB_MUX_FOREACH_MUX(op)                                              \
	COND_CODE_0(                                                         \
		IS_EMPTY(USB_MUX_LIST),                                      \
		(FOR_EACH_FIXED_ARG(USB_MUX_CALL_OP, (), op, USB_MUX_LIST)), \
		(EMPTY))

/**
 * @brief Initialise chain structure for @p idx mux
 *
 * @param chain_id Chain DTS node ID
 * @param idx USB mux index
 */
#define USB_MUX_CHAIN_STRUCT_INIT(chain_id, idx)                   \
	{                                                          \
		.mux = USB_MUX_POINTER(chain_id, idx),             \
		.next = USB_MUX_CHAIN_NEXT_POINTER(chain_id, idx), \
	}

/**
 * @brief Helper macro to set chain structure value for @p idx mux
 *
 * @param chain_id Chain DTS node ID
 * @param idx USB mux index
 */
#define USB_MUX_CHAIN_STRUCT_SET(chain_id, idx) \
	(struct usb_mux_chain) USB_MUX_CHAIN_STRUCT_INIT(chain_id, idx)

/**
 * @brief Declaration of USB mux chain extern structure for @p idx mux in
 *        @p chain_id chain
 *
 * @param chain_id USB mux chain node ID
 * @param idx Place in chain
 */
#define USB_MUX_CHAIN_STRUCT_DECLARE_EXTERN_OP(chain_id, idx) \
	extern USB_MUX_CHAIN_STRUCT_DECLARE(USBC_PORT(chain_id), idx);

/**
 * @brief Declaration of USB mux chain structure for @p idx mux in @p chain_id
 *        chain
 *
 * @param chain_id USB mux chain node ID
 * @param idx Place in chain
 */
#define USB_MUX_CHAIN_STRUCT_DECLARE_OP(chain_id, idx) \
	USB_MUX_CHAIN_STRUCT_DECLARE(USBC_PORT(chain_id), idx);

/**
 * @brief Definition of USB mux chain structure for @p idx mux in @p chain_id
 *        chain
 *
 * @param chain_id USB mux chain node ID
 * @param idx Place in chain
 */
#define USB_MUX_CHAIN_STRUCT_DEFINE_OP(chain_id, idx)            \
	USB_MUX_CHAIN_STRUCT_DECLARE(USBC_PORT(chain_id), idx) = \
		USB_MUX_CHAIN_STRUCT_INIT(chain_id, idx);

/**
 * @brief Call @p op if @p idx is not 0 (is not the root mux of chain)
 *
 * @param chain_id Chain DTS node ID
 * @param unused2 This argument is expected by DT_FOREACH_PROP_ELEM_VARGS
 * @param idx Position of USB mux in chain
 * @param op Operation to perform on USB muxes
 */
#define USB_MUX_SKIP_ROOT(chain_id, unused2, idx, op) \
	COND_CODE_1(UTIL_BOOL(idx), (op(chain_id, idx)), ())

/**
 * @brief Call @p op for each mux in @p chain_id chain except the root mux
 *
 * @param chain_id Chain DTS node ID
 * @param op Operation to perform on USB muxes
 */
#define USB_MUX_FOREACH_NO_ROOT_MUX(chain_id, op) \
	DT_FOREACH_PROP_ELEM_VARGS(chain_id, usb_muxes, USB_MUX_SKIP_ROOT, op)

/**
 * @brief Create usb_muxes array entry for @p chain_id chain
 *
 * @param chain_id Chain DTS node ID
 */
#define USB_MUX_DEFINE_ROOT_MUX(chain_id) \
	[USBC_PORT(chain_id)] = USB_MUX_CHAIN_STRUCT_INIT(chain_id, 0),

/**
 * @brief Call @p op only if chain @p chain_id is not alternative
 *
 * @param chain_id Chain DTS node ID
 * @param op Operation to perform on main USB mux chain
 * @param ... Arguments to pass to the @p op operation
 */
#define USB_MUX_FOR_MAIN_CHAIN(chain_id, op, ...)         \
	COND_CODE_0(DT_PROP(chain_id, alternative_chain), \
		    (op(chain_id, ##__VA_ARGS__)), ())

/**
 * @brief Call @p op for each USB mux chain
 *
 * @param op Operation to perform on USB mux chain
 */
#define USB_MUX_FOREACH_CHAIN(op) \
	DT_FOREACH_STATUS_OKAY(cros_ec_usb_mux_chain, op)

/**
 * @brief Call @p op for each USB mux chain with arguments
 *
 * @param op Operation to perform on USB mux chain
 * @param ... Arguments to pass to the @p op operation
 */
#define USB_MUX_FOREACH_CHAIN_VARGS(op, ...) \
	DT_FOREACH_STATUS_OKAY_VARGS(cros_ec_usb_mux_chain, op, __VA_ARGS__)

/**
 * @brief Construct first half of conditional expression (?:) that evaluates to
 *        @p chain_id USB port if @p idx mux in @p chain_id is the same as
 *        @p mux_id
 *
 * @param chain_id USB mux chain node ID
 * @param unused2 This argument is expected by DT_FOREACH_PROP_ELEM_VARGS
 * @param idx Position of USB mux in chain
 * @param mux_id USB mux node ID to compare with @p idx mux
 */
#define USB_MUX_PORT_IF_SAME_NODES(chain_id, unused2, idx, mux_id) \
	DT_SAME_NODE(mux_id, USB_MUX_GET_CHAIN_N(chain_id, idx)) ? \
		USBC_PORT(chain_id):

/**
 * @brief Compare @p mux_id with all muxes in @p chain_id
 *
 * @param chain_id USB mux chain node ID
 * @param mux_id USB mux node ID
 */
#define USB_MUX_FIND_PORT(chain_id, mux_id)             \
	DT_FOREACH_PROP_ELEM_VARGS(chain_id, usb_muxes, \
				   USB_MUX_PORT_IF_SAME_NODES, mux_id)

/**
 * @brief Get port for @p mux_id by looking for an usb mux chain where @p mux_id
 *        is present. If the mux is not present in any chain, this macro
 *        evaluate to -1.
 *
 * This expands to:
 *     (DT_DEP_ORD(mux_id) == DT_DEP_ORD(USB_MUX_GET_CHAIN_N(chain1_id, 0))) ?
 *         USBC_PORT(chain1_id) :
 *     (DT_DEP_ORD(mux_id) == DT_DEP_ORD(USB_MUX_GET_CHAIN_N(chain1_id, 1))) ?
 *         USBC_PORT(chain1_id) :
 *         ...
 *     (DT_DEP_ORD(mux_id) == DT_DEP_ORD(USB_MUX_GET_CHAIN_N(chain1_id, n))) ?
 *         USBC_PORT(chain1_id) :
 *     (DT_DEP_ORD(mux_id) == DT_DEP_ORD(USB_MUX_GET_CHAIN_N(chain2_id, 0))) ?
 *         USBC_PORT(chain2_id) :
 *         ...
 *     (DT_DEP_ORD(mux_id) == DT_DEP_ORD(USB_MUX_GET_CHAIN_N(chainm_id, k))) ?
 *         USBC_PORT(chainm_id) : (-1)
 *
 * @param mux_id USB mux node ID
 */
#define USB_MUX_PORT(mux_id) \
	(USB_MUX_FOREACH_CHAIN_VARGS(USB_MUX_FIND_PORT, mux_id)(-1))

/**
 * @brief Set usb_mux_chain structure for mux @p idx in chain @p chain_id
 *
 * @param chain_id Alternative USB mux chain node ID
 * @param idx Position of the mux in chain
 */
#define USB_MUX_SET_ALTERNATIVE(chain_id, idx)                \
	USB_MUX_CHAIN_STRUCT_NAME(idx, USBC_PORT(chain_id)) = \
		USB_MUX_CHAIN_STRUCT_SET(chain_id, idx);

/**
 * @brief Enable alternative USB mux chain
 *
 * @param chain_id Alternative USB mux chain node ID
 */
#define USB_MUX_ENABLE_ALTERNATIVE_NODE(chain_id)                              \
	do {                                                                   \
		usb_muxes[USBC_PORT(chain_id)] =                               \
			USB_MUX_CHAIN_STRUCT_SET(chain_id, 0);                 \
		USB_MUX_FOREACH_NO_ROOT_MUX(chain_id, USB_MUX_SET_ALTERNATIVE) \
	} while (0)

/**
 * @brief Enable alternative USB mux chain
 *
 * @param nodelabel Label of alternative USB mux chain
 */
#define USB_MUX_ENABLE_ALTERNATIVE(nodelabel) \
	USB_MUX_ENABLE_ALTERNATIVE_NODE(DT_NODELABEL(nodelabel))

/**
 * Forward declare all usb_mux structures e.g.
 * MAYBE_CONST struct usb_mux USB_MUX_NODE_<node_id>;
 */
USB_MUX_FOREACH_MUX(USB_MUX_DECLARE)

/**
 * Forward declare all usb_mux_chain structures e.g.
 * extern MAYBE_CONST struct usb_mux_chain
 * USB_MUX_chain_port_<node_id>_mux_<position_id>;
 */
USB_MUX_FOREACH_CHAIN_VARGS(USB_MUX_FOREACH_NO_ROOT_MUX,
			    USB_MUX_CHAIN_STRUCT_DECLARE_EXTERN_OP)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_CHROME_USBC_USB_MUXES_H */
