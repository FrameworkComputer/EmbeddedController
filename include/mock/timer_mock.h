/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_TIMER_MOCK_H
#define __MOCK_TIMER_MOCK_H

#include "timer.h"

void set_time(timestamp_t now_);

timestamp_t get_time(void);

#endif  /* __MOCK_TIMER_MOCK_H */
