/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * GPIO LED control.
 */

#define DT_DRV_COMPAT cros_ec_gpio_led_pins

#include "drivers/led.h"
#include "ec_commands.h"
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

static void gpio_set_color_with_pattern(void *p);
static void gpio_asynchronous_apply_color(bool tmp);
static void gpio_set_color(enum led_color color, enum ec_led_id led_id,
			   uint8_t brightness);
static void gpio_get_brightness_range(enum ec_led_id led_id,
				      uint8_t *brightness_range);
static int gpio_set_brightness(enum ec_led_id led_id,
			       const uint8_t *brightness);

static const struct led_driver_api gpio_led_driver_api = {
	.asynchronous_apply_color = gpio_asynchronous_apply_color,
	.set_color_with_pattern = gpio_set_color_with_pattern,
	.set_color = gpio_set_color,
	.get_brightness_range = gpio_get_brightness_range,
	.set_brightness = gpio_set_brightness,
};

/* Generate one handle for the driver instance */
const struct led_driver_t PINS_NODE(DT_DRV_INST(0)) = {
	.led_id_mask = GET_DRIVER_ID_MASK(0),
	.api = &gpio_led_driver_api,
};

/* EC_LED_COLOR maps to LED_COLOR - 1 */
#define SET_PIN_NODE(node_id)                                   \
	{                                                       \
		.led_color = GET_PROP(node_id, led_color),      \
		.led_id = GET_PROP(DT_PARENT(node_id), led_id), \
		.color_idx = DT_NODE_CHILD_IDX(node_id),        \
		.pins = PINS_ARRAY(node_id),                    \
		.pins_count = DT_PROP_LEN(node_id, led_values), \
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
	struct gpio_pin_t *gpio_pins = (struct gpio_pin_t *)pins_node->pins;

	for (int j = 0; j < pins_node->pins_count; j++) {
		gpio_pin_set_dt(gpio_get_dt_spec(gpio_pins[j].signal),
				gpio_pins[j].val);
	}
}

/*
 * Iterate through LED pins nodes to find the color matching node.
 * brightness unused, do not use brightness = 0 to turn LED off, use color =
 * LED_OFF
 */
static void gpio_set_color(enum led_color color, enum ec_led_id led_id,
			   uint8_t brightness)
{
	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		if ((pins_node[i]->led_color == color) &&
		    (pins_node[i]->led_id == led_id)) {
			led_set_color_with_node(pins_node[i]);
			break;
		}
	}
}

/*
 * The pins_node array flattens all color nodes across all LEDs. Since color_idx
 * (DT_NODE_CHILD_IDX) is only unique within a specific LED parent, should match
 * both led_id and color_idx to find the correct hardware pins. This run-time
 * lookup is for optimizing flash usage instead of storing 32-bit pointers.
 */
static const struct led_pins_node_t *gpio_find_pins_node(enum ec_led_id led_id,
							 uint8_t color_idx)
{
	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		if (pins_node[i]->led_id == led_id &&
		    pins_node[i]->color_idx == color_idx) {
			return pins_node[i];
		}
	}
	return NULL;
}

static void gpio_set_color_with_pattern(void *p)
{
	const struct led_pattern_node_t *led = (struct led_pattern_node_t *)p;
	uint8_t color_idx = led->pattern_color[led->cur_color].color_idx;

	const struct led_pins_node_t *pins_node_ptr =
		gpio_find_pins_node(led->led_id, color_idx);

	if (pins_node_ptr) {
		led_set_color_with_node(pins_node_ptr);
	}
}

static void gpio_get_brightness_range(enum ec_led_id led_id,
				      uint8_t *brightness_range)
{
	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->led_color - 1;

		if (br_color != EC_LED_COLOR_INVALID)
			brightness_range[br_color] = 1;
	}
}

static int gpio_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	bool color_set = false;

	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->led_color - 1;

		if ((br_color != EC_LED_COLOR_INVALID) &&
		    (brightness[br_color] != 0)) {
			color_set = true;
			led_set_color(pins_node[i]->led_color, led_id, 100);
		}
	}

	/* If no color was set, turn off the LED */
	if (!color_set)
		led_set_color(LED_OFF, led_id, 100);

	return EC_SUCCESS;
}

int gpio_is_supported(enum ec_led_id led_id)
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

// LCOV_EXCL_START
/* Called by hook task every HOOK_TICK_INTERVAL_MS */
static void gpio_asynchronous_apply_color(bool tmp)
{
	/*
	 * GPIO LEDs can be applied when they are set and does not need to be
	 * applied asynchronously. This function is left empty on purpose.
	 */
}
// LCOV_EXCL_STOP
