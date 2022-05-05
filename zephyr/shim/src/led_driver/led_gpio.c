/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * GPIO LED control.
 */

#include "ec_commands.h"
#include "led.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO_LED)

LOG_MODULE_REGISTER(gpio_led, LOG_LEVEL_ERR);

#define SET_PIN(node_id, prop, i)					\
{									\
	.signal = GPIO_SIGNAL(DT_PHANDLE_BY_IDX(node_id, prop, i)),	\
	.val = DT_PHA_BY_IDX(node_id, prop, i, value)			\
},

#define SET_GPIO_PIN(node_id)						\
{									\
	DT_FOREACH_PROP_ELEM(node_id, led_pins, SET_PIN)		\
};

#define GEN_PINS_ARRAY(id)						\
struct gpio_pin_t PINS_ARRAY(id)[] = SET_GPIO_PIN(id)

DT_FOREACH_CHILD(GPIO_LED_PINS_NODE, GEN_PINS_ARRAY)

#define SET_PIN_NODE(node_id)						\
{									\
	.led_color = GET_PROP(node_id, led_color),			\
	.led_id = GET_PROP(node_id, led_id),				\
	.br_color = GET_PROP_NVE(node_id, br_color),			\
	.gpio_pins = PINS_ARRAY(node_id),				\
	.pins_count = DT_PROP_LEN(node_id, led_pins)			\
};

/*
 * Initialize led_pins_node_t struct for each pin node defined
 */
#define GEN_PINS_NODES(id)						\
const struct led_pins_node_t PINS_NODE(id) = SET_PIN_NODE(id)

DT_FOREACH_CHILD(GPIO_LED_PINS_NODE, GEN_PINS_NODES)

/*
 * Array of pointers to each pin node
 */
#define PINS_NODE_PTR(id)	&PINS_NODE(id),
const struct led_pins_node_t *pins_node[] = {
	DT_FOREACH_CHILD(GPIO_LED_PINS_NODE, PINS_NODE_PTR)
};

/*
 * Set all the GPIO pins defined in the node to the defined value,
 * to enable the color.
 */
void led_set_color_with_node(const struct led_pins_node_t *pins_node)
{
	for (int j = 0; j < pins_node->pins_count; j++) {
		gpio_pin_set_dt(gpio_get_dt_spec(
			pins_node->gpio_pins[j].signal),
			pins_node->gpio_pins[j].val);
	}
}

/*
 * Iterate through LED pins nodes to find the color matching node.
 */
void led_set_color(enum led_color color, enum ec_led_id led_id)
{
	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		if ((pins_node[i]->led_color == color) &&
		    (pins_node[i]->led_id == led_id)) {
			led_set_color_with_node(pins_node[i]);
			break;
		}
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->br_color;

		if (br_color != -1)
			brightness_range[br_color] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	bool color_set = false;

	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->br_color;

		if ((br_color != -1) && (brightness[br_color] != 0)) {
			color_set = true;
			led_set_color(pins_node[i]->led_color, led_id);
		}
	}

	/* If no color was set, turn off the LED */
	if (!color_set)
		led_set_color(LED_OFF, led_id);

	return EC_SUCCESS;
}

__override int led_is_supported(enum ec_led_id led_id)
{
	static int supported_leds = -1;

	if (supported_leds == -1) {
		supported_leds = 0;

		for (int i = 0; i < ARRAY_SIZE(pins_node); i++)
			supported_leds |= (1 << pins_node[i]->led_id);
	}

	return ((1 << (int)led_id) & supported_leds);
}
#endif /* DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO_LED) */
