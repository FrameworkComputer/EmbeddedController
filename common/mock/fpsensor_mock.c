/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

/* No-op mocks */
int fp_sensor_init(void)
{
	return EC_SUCCESS;
}

int fp_sensor_deinit(void)
{
	return EC_SUCCESS;
}
