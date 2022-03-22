/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_GPIO_H__
#define __CROS_EC_LED_GPIO_H__

#define COMPAT_GPIO	cros_ec_gpio_leds

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO)

/*
 * Enum representing the GPIO LED drivers instances.
 * One is created for each instance of the GPIO LED drivers.
 */
enum led_gpio_driver {
	DT_FOREACH_STATUS_OKAY(COMPAT_GPIO, GEN_TYPE_INDEX_ENUM)
};

/*
 * GPIO LED functions.
 */
void gpio_led_init(void);
void gpio_get_led_brightness_max(enum led_gpio_driver h, uint8_t *br);
void gpio_set_led_brightness(enum led_gpio_driver h, const uint8_t *br);
void gpio_set_led_colors(enum led_gpio_driver h, const uint8_t *colors);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO) */

#endif /* __CROS_EC_LED_GPIO_H__ */
