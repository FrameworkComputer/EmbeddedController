/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BUTTON_CONFIG_H
#define __BUTTON_CONFIG_H

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "include/ec_commands.h"

#define BUTTON_CFG_COMPAT cros_ec_button_cfg
#define DT_BUTTON_CFG_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(BUTTON_CFG_COMPAT)

#define BUTTON_CFG_ENUM(val) DT_CAT(BUTTON_CFG_, val)
#define BUTTON_CFG_TYPE(node) \
	BUTTON_CFG_ENUM(DT_STRING_UPPER_TOKEN(node, button_name)),

enum button_cfg_type {
#if DT_NODE_EXISTS(DT_BUTTON_CFG_NODE)
	DT_FOREACH_CHILD(DT_BUTTON_CFG_NODE, BUTTON_CFG_TYPE)
#endif
		BUTTON_CFG_ENUM(COUNT),
};

#define BUTTON_FLAG_ACTIVE_HIGH BIT(0)
#define BUTTON_FLAG_DISABLED BIT(1) /* Button disabled */

struct button_config_v2 {
	const char *name;
	enum keyboard_button_type type;
	uint32_t debounce_us;
	uint8_t button_flags;
	enum gpio_signal gpio;
	const struct gpio_dt_spec spec;
	void (*gpio_int_handler)(enum gpio_signal);
	gpio_flags_t gpio_int_flags;
};

/**
 * @brief Get the button cfg
 *
 * @param type
 * @return const struct button_config_v2*
 */
const struct button_config_v2 *button_cfg_get(enum button_cfg_type type);

/**
 * @brief Get button name
 *
 * @param type
 * @return const char*
 */
const char *button_get_name(enum button_cfg_type type);

/**
 * @brief Get button debounce time in microseconds
 *
 * @param type
 * @return int
 */
int button_get_debounce_us(enum button_cfg_type type);

/**
 * @brief Enable interrupt for button
 *
 * @param type
 */
int button_enable_interrupt(enum button_cfg_type type);

/**
 * @brief Disable interrupt for button
 *
 * @param type
 */
int button_disable_interrupt(enum button_cfg_type type);

/**
 * @brief Get the logical level of button press
 *
 * @param type
 * @return int
 */
int button_is_pressed(enum button_cfg_type type);

/**
 * @brief Get the physical level of button press
 *
 * @param type
 * @return int
 */
int button_is_pressed_raw(enum button_cfg_type type);

#endif /* __BUTTON_CONFIG_H */
