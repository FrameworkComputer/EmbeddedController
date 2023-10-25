/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CEC_CHIP_H
#define __CROS_EC_CEC_CHIP_H

/* 1:1 conversion between us and ticks for testing purposes */

/* Time in us to timer clock ticks */
#define CEC_US_TO_TICKS(t) (t)
#ifdef CONFIG_CEC_DEBUG
/* Timer clock ticks to us */
#define CEC_TICKS_TO_US(ticks) (ticks)
#endif

#endif /* __CROS_EC_CEC_CHIP_H */
