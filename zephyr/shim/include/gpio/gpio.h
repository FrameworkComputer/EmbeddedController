/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_GPIO_GPIO_H_
#define ZEPHYR_SHIM_INCLUDE_GPIO_GPIO_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validate interrupt flags are valid for the Zephyr GPIO driver.
 */
#define IS_GPIO_INTERRUPT_FLAG(flag, mask) ((flag & mask) == mask)
#define VALID_GPIO_INTERRUPT_FLAG(flag)                             \
	(IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_RISING) ||      \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_FALLING) ||     \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_BOTH) ||        \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_LEVEL_LOW) ||        \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_LEVEL_HIGH) ||       \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_TO_INACTIVE) || \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_TO_ACTIVE) ||   \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_LEVEL_INACTIVE) ||   \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_LEVEL_ACTIVE))

/* Information about each unused pin in the 'unused-pins' device tree node. */
struct unused_pin_config {
	/* Device name of a unused gpio pin */
	const char *dev_name;
	/* Bit number of pin within a unused gpio pin */
	gpio_pin_t pin;
	/* Config flags of unused gpio pin */
	gpio_flags_t flags;
};

/**
 * @brief Set proper configuration for all unused pins.
 *
 * This function loops through all unused GPIOs in the node of "unused-gpios"
 * in the device tree file to set proper configuration. If the GPIO flag is 0,
 * set the GPIOs default setting for floating IOs to improve the power
 * consumption.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported gpio device.
 * @retval -EIO I/O error when accessing an external GPIO chip.
 */
int gpio_config_unused_pins(void) __attribute__((weak));

/**
 * @brief Set configuration by port and pin of gpio
 *
 * @param port GPIO device index
 * @param pin Pin number.
 * @param flags Flags for pin configuration: 'GPIO input/output configuration
 *        flags', 'GPIO pin drive flags', 'GPIO pin bias flags'.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP if any of the configuration options is not supported
 *                  (unless otherwise directed by flag documentation).
 * @retval -EINVAL Invalid argument.
 * @retval -EIO I/O error when accessing an external GPIO chip.
 * @retval -EWOULDBLOCK if operation would block.
 */
int gpio_configure_port_pin(int port, int id, int flags) __attribute__((weak));

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(unused_gpios) <= 1,
	     "at most one unused-gpios compatible node may be present");

#define UNUSED_GPIOS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(unused_gpios)

#if DT_NODE_EXISTS(UNUSED_GPIOS_NODE)
/**
 * @brief Length of 'unused-gpios' property
 *
 * @return length of 'unused-gpios' prop which type is 'phandle-array'
 */
#define UNUSED_GPIOS_LIST_LEN DT_PROP_LEN(UNUSED_GPIOS_NODE, unused_gpios)

/**
 * @brief Construct a unused_pin_config structure from 'unused-gpios' property
 *        at index 'i'
 *
 * @param i index of 'unused-gpios' prop which type is 'phandles-array'
 * @return unused_pin_config item at index 'i'
 */
#define UNUSED_GPIO_CONFIG_BY_IDX(i, _)                                        \
	{                                                                      \
		.dev_name = DEVICE_DT_NAME(DT_GPIO_CTLR_BY_IDX(                \
			UNUSED_GPIOS_NODE, unused_gpios, i)),                  \
		.pin = DT_GPIO_PIN_BY_IDX(UNUSED_GPIOS_NODE, unused_gpios, i), \
		.flags = DT_GPIO_FLAGS_BY_IDX(UNUSED_GPIOS_NODE, unused_gpios, \
					      i),                              \
	},

/**
 * @brief Macro function to construct a list of unused_pin_config items by
 *        LISTIFY func.
 *
 * Example devicetree fragment:
 *    / {
 *          unused-pins {
 *		compatible = "unused-gpios";
 *		unused-gpios = <&gpio5 1 0>,
 *			       <&gpiod 0 0>,
 *			       <&gpiof 3 0>;
 *	};
 *
 * Example usage:
 * static const struct unused_pin_config unused_pin_configs[] = {
 * 	UNUSED_GPIO_CONFIG_LIST
 * };
 *
 * @return a list of unused_pin_config items
 */
#define UNUSED_GPIO_CONFIG_LIST \
	LISTIFY(UNUSED_GPIOS_LIST_LEN, UNUSED_GPIO_CONFIG_BY_IDX, (), _)

#else
#define UNUSED_GPIO_CONFIG_LIST /* Nothing if no 'unused-pins' node */
#endif /* DT_NODE_EXISTS(UNUSED_GPIOS_NODE) */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SHIM_INCLUDE_GPIO_GPIO_H_ */
