/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Device event module for Chrome EC */

#ifndef __CROS_EC_DEVICE_EVENT_H
#define __CROS_EC_DEVICE_EVENT_H

#include "common.h"
#include "ec_commands.h"

/**
 * Return the raw device event state.
 */
uint32_t device_get_events(void);

/**
 * Set one or more device event bits.
 *
 * @param mask          Event bits to set (use EC_DEVICE_EVENT_MASK()).
 */
void device_set_events(uint32_t mask);

/**
 * Clear one or more device event bits.
 *
 * @param mask          Event bits to clear (use EC_DEVICE_EVENT_MASK()).
 *                      Write 1 to a bit to clear it.
 */
void device_clear_events(uint32_t mask);

/**
 * Set a single device event.
 *
 * @param event         Event to set (EC_DEVICE_EVENT_*).
 */
static inline void device_set_single_event(int event)
{
	device_set_events(EC_DEVICE_EVENT_MASK(event));
}

#endif  /* __CROS_EC_DEVICE_EVENT_H */
