/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#ifndef _WATCHDOG_H
#define _WATCHDOG_H

/* Reload the watchdog counter */
void watchdog_reload(void);

/**
 * Initialize the watchdog
 * with a reloading period of <period_ms> milliseconds.
 * It reboots the CPU if the counter has not been reloaded for twice the period.
 */
int watchdog_init(int period_ms);

#endif /* _WATCHDOG_H */
