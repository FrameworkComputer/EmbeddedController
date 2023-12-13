/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CEC_CHIP_H
#define __CROS_EC_CEC_CHIP_H

#include "hwtimer_chip.h"

#define CEC_CLOCK_SOURCE EXT_PSR_32P768K_HZ
#define CEC_CLOCK_FREQ_HZ 32768

/* Time in us to timer clock ticks */
#define CEC_US_TO_TICKS(t) ((t) * CEC_CLOCK_FREQ_HZ / 1000000)
#ifdef CONFIG_CEC_DEBUG
/* Timer clock ticks to us */
#define CEC_TICKS_TO_US(ticks) (1000000 * (ticks) / CEC_CLOCK_FREQ_HZ)
#endif

#endif /* __CROS_EC_CEC_CHIP_H */
