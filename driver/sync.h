/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Sync event driver.
 * Useful for recording the exact time a gpio interrupt happened in the
 * context of sensors. Originally created for a camera vsync signal.
 */

#ifndef __CROS_EC_VSYNC_H
#define __CROS_EC_VSYNC_H

extern const struct accelgyro_drv sync_drv;

void sync_interrupt(enum gpio_signal signal);

#endif	/* __CROS_EC_VSYNC_H */

