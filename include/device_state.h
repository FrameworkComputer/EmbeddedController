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
	const char *name;		/* Device name */
	enum device_state state;	/* Device status */
	enum device_state last_known_state;	/* Either off or on */
	/* Deferred handler to detect power off */
	const struct deferred_data *deferred;
	enum gpio_signal detect;	/* GPIO detecting power on */
};

enum device_type;

extern struct device_config device_states[];

/* Return the device state */
int device_get_state(enum device_type device);

/**
 * Sets the device state
 *
 * @param device	the device to update
 * @param state		the new device state
 */
void device_set_state(enum device_type device, enum device_state state);

/* Update the device state based on the device gpios */
void board_update_device_state(enum device_type device);
#endif  /* __CROS_DEVICE_STATE_H */
