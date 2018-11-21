/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for tablet_mode.c */

#ifdef CONFIG_TABLET_MODE

/* Return 1 if in tablet mode, 0 otherwise */
int tablet_get_mode(void);
void tablet_set_mode(int mode);

/**
 * Interrupt service routine for hall sensor.
 *
 * HALL_SENSOR_GPIO_L must be defined.
 *
 * @param signal: GPIO signal
 */
void hall_sensor_isr(enum gpio_signal signal);

/**
 * Disables the interrupt on GPIO connected to hall sensor. Additionally, it
 * disables the tablet mode switch sub-system and turns off tablet mode. This is
 * useful when the same firmware is shared between convertible and clamshell
 * devices to turn off hall sensor and tablet mode detection on clamshell.
 */
void hall_sensor_disable(void);

#else

static inline int tablet_get_mode(void) { return 0; }

#endif
