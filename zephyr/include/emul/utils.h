/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Utils for emulators.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_UTILS_H_
#define ZEPHYR_INCLUDE_EMUL_UTILS_H_

/**
 * @brief Helper macro for EMUL_GET_USBC_PROP_BINDING.
 *
 * If the usbc-port-number @p usbc_id has the same phandle in the @p chip
 * property as the @p node, then the chip binding of the usbc-port-number is
 * returned.
 *
 * @param usbc_id Named usbc port ID.
 * @param chip Name of chip phandle property to match.
 * @param node Emulator node to match the phandle of property @p chip in
 * named-usbc-port.
 * @param prop The name of the returned chip binding.
 *
 * @return The chip binding of the usbc-port-number, or NULL if no match is
 * found.
 */
#define EMUL_GET_USBC_PROP_BINDING_IF_NODE_MATCH(usbc_id, chip, node, prop) \
	(DT_DEP_ORD(node) ==                                                \
	 DT_DEP_ORD(COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, chip),            \
				(DT_PHANDLE(usbc_id, chip)), (DT_ROOT)))) ? \
		EMUL_DT_GET(DT_PROP_BY_IDX(usbc_id, prop, 0)):

/**
 * @brief Get emulator binding from the property of named-usbc-port @p prop if
 * @p chip and the emulator node @p node matched.
 *
 * @param chip Name of chip property that is phandle to required emulator.
 * @param node Node of the emulator which is the phandle of the prop of the
 * named-usbc-port.
 * @param prop The chip binding of the named-usbc-port which matched chip.
 */
#define EMUL_GET_USBC_PROP_BINDING(chip, node, prop)                       \
	(DT_FOREACH_STATUS_OKAY_VARGS(                                     \
		named_usbc_port, EMUL_GET_USBC_PROP_BINDING_IF_NODE_MATCH, \
		chip, node, prop) NULL)
#endif /* ZEPHYR_INCLUDE_EMUL_UTILS_H_ */
