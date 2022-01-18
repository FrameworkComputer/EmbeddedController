/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "motion_sense_fifo.h"

static void motion_sense_fifo_reset_before(const struct ztest_unit_test *test,
					    void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	motion_sense_fifo_reset();
}
ZTEST_RULE(motion_sense_fifo_reset, motion_sense_fifo_reset_before, NULL);
