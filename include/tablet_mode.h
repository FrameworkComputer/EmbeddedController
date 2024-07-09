/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TABLET_MODE_H
#define __CROS_EC_TABLET_MODE_H

#include "common.h"

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get tablet mode state
 *
 * Return 1 if in tablet mode, 0 otherwise
 */
int tablet_get_mode(void);

/* Bit mask of tablet mode trigger */
#define TABLET_TRIGGER_LID BIT(0)
#define TABLET_TRIGGER_BASE BIT(1)

/**
 * Set tablet mode state
 *
 * @param mode 1: tablet mode. 0 clamshell mode.
 * @param trigger: bitmask of the trigger, TABLET_TRIGGER_*.
 */
void tablet_set_mode(int mode, uint32_t trigger);

/**
 * Disable tablet mode
 */
void tablet_disable(void);

/**
 * Interrupt service routine for gmr sensor.
 *
 * GPIO_TABLET_MODE_L must be defined.
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
 * 360 sensor is active: !gpio_get_level(GPIO_TABLET_MODE_L).
 *
 * Returns 1 if the 360 sensor is active; otherwise 0.
 */
int board_sensor_at_360(void);

/** Reset internal tablet mode state, used for testing. */
__test_only void tablet_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_TABLET_MODE_H */
