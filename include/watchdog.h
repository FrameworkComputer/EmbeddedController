/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#ifndef __CROS_EC_WATCHDOG_H
#define __CROS_EC_WATCHDOG_H

/* Initialize the watchdog.  This will cause the CPU to reboot if it has been
 * more than 2 watchdog periods since watchdog_reload() has been called. */
int watchdog_init(int period_ms);

/* Reload the watchdog counter */
void watchdog_reload(void);

#endif /* __CROS_EC_WATCHDOG_H */
