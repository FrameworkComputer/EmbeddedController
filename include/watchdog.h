/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#ifndef __CROS_EC_WATCHDOG_H
#define __CROS_EC_WATCHDOG_H

/* Watchdog period in ms; must be at least twice HOOK_TICK_INTERVAL */
#define WATCHDOG_PERIOD_MS 1100

/**
 * Initialize the watchdog.
 *
 * This will cause the CPU to reboot if it has been more than 2 watchdog
 * periods since watchdog_reload() has been called.
 */
int watchdog_init(void);

/**
 * Display a trace with information about an expired watchdog timer
 *
 * This shows the location in the code where the expiration happened.
 * Usually this helps locate a loop which is blocking execution of the
 * watchdog task.
 *
 * @param excep_lr	Value of lr to indicate caller return
 * @param excep_sp	Value of sp to indicate caller task id
 */
void watchdog_trace(uint32_t excep_lr, uint32_t excep_sp);

/* Reload the watchdog counter */
void watchdog_reload(void);

#endif /* __CROS_EC_WATCHDOG_H */
