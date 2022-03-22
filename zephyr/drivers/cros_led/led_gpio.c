/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <init.h>
#include <drivers/gpio.h>

#include "led_common.h"

#include "led.h"
#include "led_gpio.h"

#include <logging/log.h>

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO)

LOG_MODULE_DECLARE(led, LOG_LEVEL_ERR);

/*
 * LED driver that control LED via GPIOs, defined
 * via the cros_ec_gpio_leds compatible.
 */

/*
 * GPIO LED driver read-only configuration.
 * Stores the DTS originated configuration for each
 * driver for the GPIO LED driver.
 */
struct led_gpio {
	uint8_t gpio_count;	/* Number of GPIOs */
	const struct gpio_dt_spec *gpios;
	const uint8_t *color_map;
};

#define LED_GPIO_COLOR_MAP(id)	DT_CAT(L_C_M_, id)
#define LED_GPIO_SPECS(id)       DT_CAT(L_G_S_, id)

/*
 * Generate the GPIO list for each driver instance.
 * This list captures the GPIO configuration for
 * each GPIO used to drive the LEDs.
 * The list is named using a unique name based on the node name
 * so that it can be referenced later.
 */
#define GEN_GPIO_ENTRY(id, p, idx)				\
	GPIO_DT_SPEC_GET_BY_IDX(id, p, idx),			\

#define GEN_GPIO_TABLE_ENTRY(id)				\
static const struct gpio_dt_spec LED_GPIO_SPECS(id)[] = {		\
	DT_FOREACH_PROP_ELEM(id, gpios, GEN_GPIO_ENTRY)		\
};

DT_FOREACH_STATUS_OKAY(COMPAT_GPIO, GEN_GPIO_TABLE_ENTRY)

/*
 * Generate byte array holding color map for
 * EC host command brightness mapping.
 */
#define GEN_GPIO_COLOR_MAP(id)					\
static const uint8_t LED_GPIO_COLOR_MAP(id)[] = DT_PROP(id, color_list);

DT_FOREACH_STATUS_OKAY(COMPAT_GPIO, GEN_GPIO_COLOR_MAP)

/*
 * Generate driver structures. These represent the
 * top level GPIO LED configuration, and contains
 * the list of GPIOs for each driver instance.
 */
#define GEN_GPIO_TABLE(id)					\
{								\
	.gpio_count = DT_PROP_LEN(id, gpios),			\
	.gpios = LED_GPIO_SPECS(id),				\
	.color_map = LED_GPIO_COLOR_MAP(id),			\
},

static const struct led_gpio gpio_driver[] = {
DT_FOREACH_STATUS_OKAY(COMPAT_GPIO, GEN_GPIO_TABLE)
};

/*
 * Update the LED GPIO settings using the GPIO outputs provided.
 */
static void set_gpio_led_values(const struct led_gpio *gpio,
				const uint8_t *colors)
{
	const struct gpio_dt_spec *gp = gpio->gpios;

	for (int i = 0; i < gpio->gpio_count; i++, gp++) {
		gpio_pin_set_dt(gp, *colors++);
	}
}

void gpio_get_led_brightness_max(enum led_gpio_driver h, uint8_t *br)
{
	const struct led_gpio *gpio = &gpio_driver[h];
	const uint8_t *cmp = gpio->color_map;
	/*
	 * Walk through the color_map for the GPIO LED and
	 * set the brightness range to 1 for each
	 * of the supported colors.
	 */
	for (int i = 0; i < gpio->gpio_count; i++) {
		br[cmp[i]] = 1;
	}
}

void gpio_set_led_brightness(enum led_gpio_driver h, const uint8_t *br)
{
	uint8_t colors[4];
	const struct led_gpio *gpio = &gpio_driver[h];
	const uint8_t *cmp = gpio->color_map;
	/*
	 * Walk through the color_map for the GPIO LED and
	 * set the GPIO value to 0 or 1.
	 */
	for (int i = 0; i < gpio->gpio_count; i++) {
		colors[i] = br[cmp[i]];
	}
	set_gpio_led_values(gpio, colors);
}

/*
 * Set the LED outputs of this GPIO LED driver.
 */
void gpio_set_led_colors(enum led_gpio_driver h, const uint8_t *colors)
{
	set_gpio_led_values(&gpio_driver[h], colors);
}

/*
 * Initialise the runtime state of the GPIO LED driver.
 */
void gpio_led_init(void)
{
	/*
	 * Configure GPIOs.
	 */
	for (int i = 0; i < ARRAY_SIZE(gpio_driver); i++) {
		const struct gpio_dt_spec *gp = gpio_driver[i].gpios;

		for (int j = 0; j < gpio_driver[i].gpio_count; j++, gp++) {
			gpio_pin_configure_dt(gp, GPIO_OUTPUT_INACTIVE);
		}
	}
}
#endif /* DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO) */
