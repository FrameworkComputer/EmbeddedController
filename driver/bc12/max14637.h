/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX14637 USB BC 1.2 Charger Detector driver definitions */

#include "gpio.h"

#define MAX14637_FLAGS_ENABLE_ACTIVE_LOW		BIT(0)
#define MAX14637_FLAGS_CHG_DET_ACTIVE_LOW		BIT(1)

struct max14637_config_t {
	/*
	 * Enable signal to BC 1.2. Can be active high or low depending on
	 * MAX14637_FLAGS_ENABLE_ACTIVE_LOW flag bit.
	 */
	enum gpio_signal chip_enable_pin;
	/*
	 * Charger detect signal from BC 1.2 chip. Can be active high or low
	 * depending on MAX14637_FLAGS_CHG_DET_ACTIVE_LOW flag bit.
	 */
	enum gpio_signal chg_det_pin;
	/* Configuration flags with prefix MAX14637_FLAGS. */
	int flags;
};

/*
 * Array that contains boards-specific configuration for BC 1.2 charging chips.
 */
extern const struct max14637_config_t
				max14637_config[CONFIG_USB_PD_PORT_MAX_COUNT];
