/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#ifndef __CROS_EC_WATCHDOG_H
#define __CROS_EC_WATCHDOG_H

#define WATCHDOG_PERIOD_MS 1100  /* Watchdog period in ms */

/*
 * Interval in ms between reloads of the watchdog timer.  Should be less
 * than half of the watchdog period.
 */
#define WATCHDOG_RELOAD_MS 500

/* Initialize the watchdog.  This will cause the CPU to reboot if it has been
 * more than 2 watchdog periods since watchdog_reload() has been called. */
int watchdog_init(void);

/* Reload the watchdog counter */
void watchdog_reload(void);

#endif /* __CROS_EC_WATCHDOG_H */
