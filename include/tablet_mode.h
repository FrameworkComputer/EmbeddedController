/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TABLET_MODE_H
#define __CROS_EC_TABLET_MODE_H

/**
 * Get tablet mode state
 *
 * Return 1 if in tablet mode, 0 otherwise
 */
int tablet_get_mode(void);

/**
 * Set tablet mode state
 *
 * @param mode 1: tablet mode. 0 clamshell mode.
 */
void tablet_set_mode(int mode);

/**
 * Disable tablet mode
 */
void tablet_disable(void);

/**
 * Interrupt service routine for gmr sensor.
 *
 * GMR_TABLET_MODE_GPIO_L must be defined.
 *
 * @param signal: GPIO signal
 */
void gmr_tablet_switch_isr(enum gpio_signal signal);

/**
 * Disables the interrupt on GPIO connected to gmr sensor. Additionally, it
 * disables the tablet mode switch sub-system and turns off tablet mode. This
 * is useful when the same firmware is shared between convertible and clamshell
 * devices to turn off gmr sensor's tablet mode detection on clamshell.
 */
void gmr_tablet_switch_disable(void);

/**
 * This must be defined when CONFIG_GMR_TABLET_MODE_CUSTOM is defined. This
 * allows a board to override the default behavior that determines if the
 * 360 sensor is active: !gpio_get_level(GMR_TABLET_MODE_GPIO_L).
 *
 * Returns 1 if the 360 sensor is active; otherwise 0.
 */
int board_sensor_at_360(void);

#endif  /* __CROS_EC_TABLET_MODE_H */
