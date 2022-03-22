# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_ALT
	led_policy.c
	led_gpio.c
	led_pwm.c
	)
