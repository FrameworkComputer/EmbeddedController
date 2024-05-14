/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#ifndef ZEPHYR_CHROME_I2C_I2C_H
#define ZEPHYR_CHROME_I2C_I2C_H

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(named_i2c_ports) == 1,
	     "only one named-i2c-ports compatible node may be present");

#define NAMED_I2C_PORTS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(named_i2c_ports)

#define NPCX_PORT_COMPAT nuvoton_npcx_i2c_port
#define ITE_IT8XXX2_PORT_COMPAT ite_it8xxx2_i2c
#define ITE_ENHANCE_PORT_COMPAT ite_enhance_i2c
#define MICROCHIP_XEC_COMPAT microchip_xec_i2c_v2
#define INTEL_SEDI_I2C_COMPAT intel_sedi_i2c
#define I2C_EMUL_COMPAT zephyr_i2c_emul_controller
#define I2C_FOREACH_PORT(fn)                                \
	DT_FOREACH_STATUS_OKAY(NPCX_PORT_COMPAT, fn)        \
	DT_FOREACH_STATUS_OKAY(ITE_IT8XXX2_PORT_COMPAT, fn) \
	DT_FOREACH_STATUS_OKAY(ITE_ENHANCE_PORT_COMPAT, fn) \
	DT_FOREACH_STATUS_OKAY(MICROCHIP_XEC_COMPAT, fn)    \
	DT_FOREACH_STATUS_OKAY(INTEL_SEDI_I2C_COMPAT, fn)   \
	DT_FOREACH_STATUS_OKAY(I2C_EMUL_COMPAT, fn)

/*
 * Get the legacy I2C port enum value from the I2C bus node identifier.
 * The value returned by this macro is passed as the 'int port' parameter to all
 * the legacy APIs provided by i2c_controller.h
 *
 * Example devicetree fragment:
 *
 *     / {
 *         soc-if {
 *             i2c2_0: io_i2c_ctrl2_port0 {
 *                 compatible = "nuvoton,npcx-i2c-port";
 *                 #address-cells = <1>;
 *                 #size-cells = <0>;
 *                 port = <0x20>;
 *                 controller = <&i2c_ctrl2>;
 *                 label = "I2C_2_PORT_0";
 *                 status = "disabled";
 *             };
 *         }.
 *     };
 *
 * Example usage to get the I2C port enum value for i2c2_0:
 *
 *     I2C_PORT_BUS(DT_NODELABEL(i2c2_0))
 *     // I2C_BUS_DT_N_S_soc_if_S_io_i2c_ctrl2_port0
 *
 * @param i2c_port_id: node id of a I2C port device
 */
#define I2C_PORT_BUS(i2c_port_id) DT_CAT(I2C_BUS_, i2c_port_id)
#define I2C_PORT_BUS_WITH_COMMA(i2c_port_id) I2C_PORT_BUS(i2c_port_id),

/*
 * Get the legacy I2C port enum value from a named-i2c-ports child node.
 *
 * Example devicetree fragment:
 *
 *     i2c0_0: io_i2c_ctrl0_port0 {
 *         compatible = "nuvoton,npcx-i2c-port";
 *         #address-cells = <1>;
 *         #size-cells = <0>;
 *         port = <0x00>;
 *         controller = <&i2c_ctrl0>;
 *         label = "I2C_0_PORT_0";
 *         status = "disabled";
 *     };
 *
 *     named-i2c-ports {
 *         compatible = "named-i2c-ports";
 *         i2c_sensor: sensor {
 *             i2c-port = <&i2c0_0>;
 *             enum-names = "I2C_PORT_SENSOR";
 *         };
 *     };
 *
 * Example usage to get the I2C port enum value for i2c_sensor:
 *
 *     I2C_PORT(DT_NODELABEL(i2c_sensor))
 *
 * which equals:
 *
 *     I2C_PORT_BUS(DT_NODELABEL(i2c0_0))
 *
 * @param i2c_named_id: node id of a child of the named-i2c-ports node
 */
#define I2C_PORT(i2c_named_id) I2C_PORT_BUS(DT_PHANDLE(i2c_named_id, i2c_port))

/*
 * Get the legacy I2C port enum from the I2C bus nodelabel. This macro should be
 * used with the I2C port device node, not the named-i2c-port child node.
 *
 *     / {
 *         soc-if {
 *             i2c2_0: io_i2c_ctrl2_port0 {
 *                 compatible = "nuvoton,npcx-i2c-port";
 *                 #address-cells = <1>;
 *                 #size-cells = <0>;
 *                 port = <0x20>;
 *                 controller = <&i2c_ctrl2>;
 *                 label = "I2C_2_PORT_0";
 *                 status = "disabled";
 *             };
 *         }.
 *     };
 *
 * Example usage to get the I2C port enum value for i2c2_0:
 *
 *     I2C_PORT_NODELABEL(i2c2_0)
 *     // I2C_BUS_DT_N_S_soc_if_S_io_i2c_ctrl2_port0
 *
 * @param label: nodelabel of a I2C port device
 */
#define I2C_PORT_NODELABEL(label) I2C_PORT_BUS(DT_NODELABEL(label))

/*
 * Get the legacy I2C port enum for a child device on an I2C bus.
 *
 * Example devicetree fragment:
 *
 *     i2c2_0: io_i2c_ctrl2_port0 {
 *         compatible = "nuvoton,npcx-i2c-port";
 *         #address-cells = <1>;
 *         #size-cells = <0>;
 *         port = <0x20>;
 *         controller = <&i2c_ctrl2>;
 *         label = "I2C_2_PORT_0";
 *         status = "disabled";
 *     };
 *
 *     &i2c2_0 {
 *         bc12_port0: pi3usb9201@5f {
 *             compatible = "pericom,pi3usb9201";
 *             status = "okay";
 *             reg = <0x5f>;
 *             irq = <&int_usb_c0_bc12>;
 *         };
 *     };
 *
 * Example usage to get the I2C port enum value for bc12_port0:
 *
 *     I2C_PORT_BY_DEV(DT_NODELABEL(bc12_port0))
 *
 *  * which equals:
 *
 *     I2C_PORT_BUS(DT_NODELABEL(i2c2_0))
 *
 * @param dev_id: node id of a device on the I2C bus
 */
#define I2C_PORT_BY_DEV(dev_id) I2C_PORT_BUS(DT_BUS(dev_id))

enum i2c_ports_chip {
	I2C_FOREACH_PORT(I2C_PORT_BUS_WITH_COMMA) I2C_PORT_COUNT
};

BUILD_ASSERT(I2C_PORT_COUNT != 0, "No I2C devices defined");

#define I2C_PORT_ENUM_IDX_COMMA(i2c_named_id, prop, idx)        \
	DT_STRING_UPPER_TOKEN_BY_IDX(i2c_named_id, prop, idx) = \
		I2C_PORT(i2c_named_id),
#define NAMED_I2C_PORT_COMMA(i2c_named_id) \
	DT_FOREACH_PROP_ELEM(i2c_named_id, enum_names, I2C_PORT_ENUM_IDX_COMMA)

/*
 * The enum i2c_ports maps the hard-coded I2C port names (such as
 * I2C_PORT_BATTERY or I2C_PORT_SENSOR) to the unique port numbers created by
 * enum i2c_ports_chip above for every I2C port devicetree node.
 */
enum i2c_ports {
	DT_FOREACH_CHILD_STATUS_OKAY(NAMED_I2C_PORTS_NODE, NAMED_I2C_PORT_COMMA)
};

/**
 * @brief Adaptation of platform/ec's port IDs which map a port/bus to a device.
 *
 * This function should be implemented per chip and should map the enum value
 * defined for the chip for encoding each valid port/bus combination. For
 * example, the npcx chip defines the port/bus combinations NPCX_I2C_PORT* under
 * chip/npcx/registers-npcx7.h.
 *
 * Thus, the npcx shim should implement this function to map the enum values
 * to the correct devicetree device.
 *
 * @param port The port to get the device for.
 * @return Pointer to the device struct or {@code NULL} if none are available.
 */
const struct device *i2c_get_device_for_port(const int port);

/**
 * @brief Get a port number for a received remote port number.
 *
 * This function translate a received port number via the I2C_PASSTHRU host
 * command to a port number used in ZephyrEC based on remote_port property in
 * dts. The first port which matches the remote port number is returned.
 *
 * @param port The received remote port.
 * @return Port number used in EC. -1 if the remote port is not defined
 */
int i2c_get_port_from_remote_port(int remote_port);

/**
 * @brief Get legacy I2C port enum from Zephyr device pointer
 *
 * @param i2c_dev Zephyr device struct pointer for the target I2C port
 * @return i2c_ports enum if match is found, or -1 if not.
 */
enum i2c_ports i2c_get_port_from_device(const struct device *i2c_dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_CHROME_I2C_I2C_H */
