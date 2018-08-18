/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_RBOX_H
#define __CROS_RBOX_H

/**
 * Return true if the power button output shows it is pressed
 */
int rbox_powerbtn_is_pressed(void);

/**
 * Clear the wakeup interrupts
 */
void rbox_clear_wakeup(void);
#endif  /* __CROS_RBOX_H */
