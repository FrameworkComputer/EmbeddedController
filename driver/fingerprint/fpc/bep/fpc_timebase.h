/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPC_TIMEBASE_H
#define __CROS_EC_FPC_TIMEBASE_H

/**
 * @file    fpc_timebase.h
 * @brief   Timebase based on a system tick.
 *
 * Supplies tick counter and wait operation(s).
 */

#include "common.h"

#include <stdint.h>

/**
 * @brief Reads the system tick counter.
 *
 * @details To handle tick counter wrap around when checking for timeout, make
 *          sure to do the calculation in the following manner:
 *          "if ((current_tick - old_tick) > timeout) {"
 *          Example: current time (uint32_t) = 10 ticks
 *                   old time (uint32_t) = 30 ticks before overflow of uint32_t
 *          current_time - old_time = 10 - (2**32 - 30) -> wraps around to 40
 *
 * @return Tick count since system startup. [ms]
 */
__staticlib_hook uint32_t fpc_timebase_get_tick(void);

/**
 * @brief Busy wait.
 *
 * @param[in] ms  Time to wait [ms].
 * 0 => return immediately
 * 1 => wait at least 1ms etc.
 */
__staticlib_hook void fpc_timebase_busy_wait(uint32_t ms);

#endif /* __CROS_EC_FPC_TIMEBASE_H */
