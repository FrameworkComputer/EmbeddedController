/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lid switch API for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_LID_SWITCH_H
#define __CROS_EC_LID_SWITCH_H

#include "common.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return non-zero if lid is open.
 *
 * Uses the debounced lid state, not the raw signal from the GPIO.
 */
int lid_is_open(void);

/**
 * Interrupt handler for lid switch.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void lid_interrupt(enum gpio_signal signal);

/**
 * Disable lid interrupt and set the lid open, when base is disconnected.
 *
 * @param enable    Flag that enables or disables lid interrupt.
 */
void enable_lid_detect(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_LID_SWITCH_H */
