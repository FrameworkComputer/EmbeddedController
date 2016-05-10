/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"

#ifndef __CROS_DEVICE_STATE_H
#define __CROS_DEVICE_STATE_H

/* Device state indexes */
enum device_state {
	DEVICE_STATE_UNKNOWN = 0,
	DEVICE_STATE_OFF,
	DEVICE_STATE_ON,
	DEVICE_STATE_COUNT,
};

struct device_config {
	enum device_state state;
	int state_change;
	const struct deferred_data *deferred;
	enum gpio_signal detect_on;
	enum gpio_signal detect_off;
};

enum device_type;

extern struct device_config device_states[];

int device_get_state(enum device_type device);

void device_set_state(enum device_type device, enum device_state state);

void board_update_device_state(enum device_type device);
#endif  /* __CROS_DEVICE_STATE_H */
