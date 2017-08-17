/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_DEVICE_STATE_H
#define __CROS_DEVICE_STATE_H

enum gpio_signal;

/* Device configuration */
struct device_config {
	/* Device name */
	const char *name;

	/* Current state */
	enum device_state state;

	/*
	 * Last known state.  That is, the last state value passed to
	 * device_set_state() which was DEVICE_STATE_OFF or DEVICE_STATE_ON.
	 * Or DEVICE_STATE_UNKNOWN, if device_set_state() has not been called
	 * for this device this boot.
	 */
	enum device_state last_known_state;

	/*
	 * Deferred handler to debounce state transitions. This is NOT used by
	 * the device_state module; it's just here as a convenience for the
	 * board.
	 */
	const struct deferred_data *deferred;

	/*
	 * GPIO used to detect the state.  This is NOT used by the device_state
	 * module; it's just here as a convenience for the board.
	 */
	enum gpio_signal detect;
};

/*
 * board.h must supply an enumerated list of devices, ending in DEVICE_COUNT.
 */
enum device_type;

/*
 * board.c must provide this list of device configurations.  It must match enum
 * device_type, and must be DEVICE_COUNT entries long.
 */
extern struct device_config device_states[];

/**
 * Get the current state for the device.
 *
 * @param device	Device to check
 * @return The device state (current; NOT last known).
 */
enum device_state device_get_state(enum device_type device);

/**
 * Set the device state
 *
 * Updates the device's last known state if <state> is DEVICE_STATE_ON or
 * DEVICE_STATE_OFF, and that's different than the device's last known state.
 *
 * Note that this only changes the recorded state.  It does not notify anything
 * of these changes.  That must be done by the caller.
 *
 * @param device	Device to update
 * @param state		New device state
 * @return non-zero if this changed the device's last known state.
 */
int device_set_state(enum device_type device, enum device_state state);

/**
 * Update the device state based on the device gpios.
 *
 * The board must implement this.  It will be called for each device in the
 * context of HOOK_SECOND.  If the state has changed, the board is responsible
 * for doing any associated reconfiguration and then calling
 * device_set_state().
 *
 * @param device	Device to check.
 */
void board_update_device_state(enum device_type device);

#endif  /* __CROS_DEVICE_STATE_H */
