/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * GPIO LED control.
 */

#define DT_DRV_COMPAT cros_ec_gpio_led_pins

#include "ec_commands.h"
#include "led.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_led, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,gpio-led-pins should be defined.");

#define SET_PIN(node_id, prop, i)                                      \
	{ .signal = GPIO_SIGNAL(                                       \
		  DT_PHANDLE_BY_IDX(DT_PARENT(node_id), led_pins, i)), \
	  .val = DT_PROP_BY_IDX(node_id, prop, i) },

#define SET_GPIO_PIN(node_id) \
	{ DT_FOREACH_PROP_ELEM(node_id, led_values, SET_PIN) };

#define GEN_PINS_ARRAY(id) struct gpio_pin_t PINS_ARRAY(id)[] = SET_GPIO_PIN(id)

DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, DT_FOREACH_CHILD, GEN_PINS_ARRAY)

/* EC_LED_COLOR maps to LED_COLOR - 1 */
#define SET_PIN_NODE(node_id)                                   \
	{                                                       \
		.led_color = GET_PROP(node_id, led_color),      \
		.led_id = GET_PROP(DT_PARENT(node_id), led_id), \
		.gpio_pins = PINS_ARRAY(node_id),               \
		.pins_count = DT_PROP_LEN(node_id, led_values)  \
	}

/*
 * Initialize led_pins_node_t struct for each pin node defined
 */
#define GEN_PINS_NODES(id) \
	const struct led_pins_node_t PINS_NODE(id) = SET_PIN_NODE(id);

DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, DT_FOREACH_CHILD, GEN_PINS_NODES)

/*
 * Array of pointers to each pin node
 */
#define PINS_NODE_PTR(id) &PINS_NODE(id),
const struct led_pins_node_t *pins_node[] = {
	DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, DT_FOREACH_CHILD,
						PINS_NODE_PTR)
};

/*
 * Set all the GPIO pins defined in the node to the defined value,
 * to enable the color.
 */
void led_set_color_with_node(const struct led_pins_node_t *pins_node)
{
	for (int j = 0; j < pins_node->pins_count; j++) {
		gpio_pin_set_dt(
			gpio_get_dt_spec(pins_node->gpio_pins[j].signal),
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

void led_set_color_with_pattern(const struct led_pattern_node_t *led)
{
	struct led_pins_node_t *pins_node =
		led->pattern_color[led->cur_color].led_color_node;
	led_set_color_with_node(pins_node);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->led_color - 1;

		if (br_color != EC_LED_COLOR_INVALID)
			brightness_range[br_color] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	bool color_set = false;

	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->led_color - 1;

		if ((br_color != EC_LED_COLOR_INVALID) &&
		    (brightness[br_color] != 0)) {
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

#ifdef TEST_BUILD
const struct led_pins_node_t *led_get_node(enum led_color color,
					   enum ec_led_id led_id)
{
	const struct led_pins_node_t *pin_node = NULL;

	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		if (pins_node[i]->led_id == led_id &&
		    pins_node[i]->led_color == color) {
			pin_node = pins_node[i];
			break;
		}
	}

	return pin_node;
}
#endif /* TEST_BUILD */

/* Called by hook task every HOOK_TICK_INTERVAL_MS */
void board_led_apply_color(void)
{
	/*
	 * GPIO LEDs can be applied when they are set and does not need to be
	 * applied asynchronously. This function is left empty on purpose.
	 */
}
