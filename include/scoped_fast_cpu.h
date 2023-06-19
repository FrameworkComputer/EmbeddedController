/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* A class to scope the range of boosting CPU. */

#ifndef __CROS_EC_SCOPED_FAST_CPU_H
#define __CROS_EC_SCOPED_FAST_CPU_H

#include "clock.h"

class ScopedFastCpu {
    public:
	ScopedFastCpu()
		: previous_state_(current_state_)
	{
		if (current_state_ != 1) {
			clock_enable_module(MODULE_FAST_CPU, 1);
			current_state_ = 1;
		}
	}
	~ScopedFastCpu()
	{
		if (current_state_ != previous_state_) {
			clock_enable_module(MODULE_FAST_CPU, previous_state_);
			current_state_ = previous_state_;
		}
	}

    private:
	int previous_state_;
	static inline int current_state_;
};

#endif /* __CROS_EC_SCOPED_FAST_CPU_H */
