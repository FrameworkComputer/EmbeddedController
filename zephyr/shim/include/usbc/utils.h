/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_SHIM_USBC_UTIL

/*
 * Enable interrupt from the `irq` property of an instance's node.
 *
 * @param inst: instance number
 */
#define BC12_GPIO_ENABLE_INTERRUPT(inst)             \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(inst, irq), \
		   (gpio_enable_dt_interrupt(        \
			    GPIO_INT_FROM_NODE(DT_INST_PHANDLE(inst, irq)));))

/*
 * Get the port number from a child of `named-usbc-port` node.
 *
 * @param id: node id
 */
#define USBC_PORT(id) DT_REG_ADDR(DT_PARENT(id))

/*
 * Get the port number from a `named-usbc-port` node.
 *
 * @param id: `named-usbc-port` node id
 */
#define USBC_PORT_NEW(id) DT_REG_ADDR(id)

/*
 * Get the port number from a child of `named-usbc-port` node.
 *
 * @param inst: instance number of the node
 */
#define USBC_PORT_FROM_INST(inst) USBC_PORT(DT_DRV_INST(inst))

/**
 * @brief Helper macro to check for the NTC38xx TCPC. The NCT38xx TCPC
 * is configured as a child binding under the nuvoton,nc38xx MFD. Grab
 * the parent phandle when the NCT38xx TCPC is detected, otherwise return
 * the current node phandle.
 */
#define DEVICE_GET_CHIP_BINDING(chip_phandle)                               \
	COND_CODE_1(DT_NODE_HAS_COMPAT(chip_phandle, nuvoton_nct38xx_tcpc), \
		    (DEVICE_DT_GET(DT_PARENT(chip_phandle))),               \
		    (DEVICE_DT_GET(chip_phandle)))

/**
 * @brief Helper macro for DEVICE_GET_USBC_BINDING. If @p usbc_id has the same
 *        port number as @p port, then struct device* for @p chip phandle is
 *        returned.
 *
 * @param usbc_id Named usbc port ID
 * @param port Port number to match with named usbc port
 * @param chip Name of chip phandle property
 */
#define DEVICE_GET_USBC_BINDING_IF_PORT_MATCH(usbc_id, port, chip) \
	COND_CODE_1(IS_EQ(USBC_PORT_NEW(usbc_id), port),           \
		    (DEVICE_GET_CHIP_BINDING(DT_PHANDLE(usbc_id, chip))), ())

/**
 * @brief Get the struct device for a phandle @p chip property of USBC @p port
 *
 * @param port Named usbc port number.  The value has to be an integer literal
 * @param chip Name of the chip property that contains a phandle of the driver.
 */
#define DEVICE_GET_USBC_BINDING(port, chip)                                 \
	DT_FOREACH_STATUS_OKAY_VARGS(named_usbc_port,                       \
				     DEVICE_GET_USBC_BINDING_IF_PORT_MATCH, \
				     port, chip)

#define NODE_MATCHES(node1, node2) IS_EQ(DT_DEP_ORD(node1), DT_DEP_ORD(node2))

#define GET_USBC_PORT_IF_MATCHES_PROP(usbc_id, nodeid, prop)         \
	COND_CODE_1(NODE_MATCHES(DT_PHANDLE(usbc_id, prop), nodeid), \
		    (USBC_PORT_NEW(usbc_id)), ())

/**
 * @brief Given a devicetree node, return the USB-C port number that references
 * the devicetree node.
 *
 * Usage:
 *	usbc_port0: port0@0 {
 *		compatible = "named-usbc-port";
 *		reg = < 0x0 >;
 *		chg = < &charger >;
 *		pdc = < &pdc_power_p0 >;
 *	};
 *	usbc_port1: port1@1 {
 *		compatible = "named-usbc-port";
 *		reg = < 0x1 >;
 *		pdc = < &pdc_power_p1 >;
 *	};
 *	&i2c{
 *		pdc_power_p1: driver@88 {
 *			compatible = "my-driver";
 *		}
 *
 *
 *
 * @param nodeid Devicetree node to search for
 * @param prop named-usbc-port property to check
 * @returns USB-C port number
 */
#define USBC_PORT_FROM_DRIVER_NODE(nodeid, prop) \
	DT_FOREACH_STATUS_OKAY_VARGS(            \
		named_usbc_port, GET_USBC_PORT_IF_MATCHES_PROP, nodeid, prop)

/*
 * Check that the TCPC interrupt flag defined in the devicetree is the same as
 * the hardware.
 *
 * @param id: node id of the tcpc port
 */
#define TCPC_VERIFY_NO_FLAGS_ACTIVE_ALERT_HIGH(id)                             \
	BUILD_ASSERT(                                                          \
		(DT_PROP(id, tcpc_flags) & TCPC_FLAGS_ALERT_ACTIVE_HIGH) == 0, \
		"TCPC interrupt configuration error for " DT_NODE_FULL_NAME(   \
			id));

/**
 * @brief Macros used to process USB-C driver organized as a
 * (compatible, config) tuple.  Where "compatible" is the devictree compatible
 * string and "config" is the macro used to initialize the USB-C driver
 * instance.
 *
 * The "config" macro has a single parameter, the devicetree node ID of the
 * USB-C device driver (not the ID of the named-usbc-port node).
 */
#define USBC_DRIVER_GET_COMPAT(driver) GET_ARG_N(1, __DEBRACKET driver)
#define USBC_DRIVER_GET_COMPAT_COMMA(driver) USBC_DRIVER_GET_COMPAT(driver),
#define USBC_DRIVER_GET_CONFIG(driver) GET_ARG_N(2, __DEBRACKET driver)
#define USBC_DRIVER_GET_CONFIG(driver) GET_ARG_N(2, __DEBRACKET driver)

/**
 * @brief Call @p op operation for each node that is compatible with @p driver
 *
 * @param driver USB driver description in format (compatible, config)
 * @param op Operation to perform on each USB device. Should accept mux node ID
 *           and driver config as arguments.
 */
#define USBC_DRIVER_CONFIG(driver, op)                                   \
	DT_FOREACH_STATUS_OKAY_VARGS(USBC_DRIVER_GET_COMPAT(driver), op, \
				     USBC_DRIVER_GET_CONFIG(driver))

/**
 * @brief Call @p op operation for each USB driver node that found in the
 *        devicetree that matches a compatible from the caller supplied
 *        driver list.
 *
 * @param op Operation to perform on each USB driver. Should accept node ID and
 *           driver config as arguments.
 * @param driver_list USB driver list, each driver in format
 *                    (compatible, config), separated by commas.
 */
#define DT_FOREACH_USBC_DRIVER_STATUS_OK_VARGS(op, driver_list) \
	FOR_EACH_FIXED_ARG(USBC_DRIVER_CONFIG, (), op, driver_list)

/**
 * @brief When processing the named-usbc-port, the node ID of the USB-C port
 * and the node ID of a property (ppc, tcpc, etc), are passed as a tuple
 * as the fixed argument to FOR_EACH_FIXED_ARG.
 *
 * The macros below extract the USB-C node ID and the property node ID
 * from this tuple.
 */
#define NODES_GET_USBC_ID(nodes) GET_ARG_N(1, __DEBRACKET nodes)
#define NODES_GET_PROP_ID(nodes) GET_ARG_N(2, __DEBRACKET nodes)

#endif /* __CROS_EC_ZEPHYR_SHIM_USBC_UTIL */
