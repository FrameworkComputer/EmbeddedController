/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#ifndef __CROS_EC_WATCHDOG_H
#define __CROS_EC_WATCHDOG_H

#include "config.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * Watchdog has not been tickled recently warning. This function should be
 * called when the watchdog is close to firing.
 */
void watchdog_warning_irq(void);

/**
 * We cannot unlock the watchdog timer within 3 watch dog ticks of
 * touching it per the datasheet. This is around 100ms so we should
 * protect against this.
 */
void watchdog_stop_and_unlock(void);

/* Reload the watchdog counter */
#ifdef CONFIG_WATCHDOG
void watchdog_reload(void);
#else
test_mockable_static_inline void watchdog_reload(void)
{
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_WATCHDOG_H */
