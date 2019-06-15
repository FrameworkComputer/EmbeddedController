/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/timer_mock.h"

static timestamp_t now;

void set_time(timestamp_t now_)
{
	now = now_;
}

timestamp_t get_time(void)
{
	return now;
};
